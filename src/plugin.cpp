// SPDX-License-Identifier: MIT OR Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
// Included from the unmodified external SDK, never vendored.
#include <f4se/PluginAPI.h>
#include <f4se_common/f4se_version.h>
#include "osp/console.hpp"
#include "osp/weapon.hpp"
#include "osp/nodes.hpp"
#include "osp/scene.hpp"
#include "osp/scene3.hpp"
#include "osp/sequencer.hpp"
#include "osp/sequencer_layout.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>

static_assert(sizeof(void*)==8);
static_assert(sizeof(F4SEInterface)==0x30 && offsetof(F4SEInterface,QueryInterface)==0x10);
static_assert(sizeof(PluginInfo)==0x18 && offsetof(PluginInfo,name)==8);
static_assert(sizeof(F4SEMessagingInterface::Message)==0x18);
static_assert(offsetof(F4SEMessagingInterface,RegisterListener)==8);
static_assert(RUNTIME_VERSION_1_10_163==0x010a0a30 && PACKED_F4SE_VERSION==0x00060170);
static_assert(F4SEMessagingInterface::kMessage_PostLoadGame==3);
static_assert(F4SEMessagingInterface::kMessage_NewGame==8);
static_assert(F4SEMessagingInterface::kMessage_PreLoadGame==2 && F4SEMessagingInterface::kMessage_PreSaveGame==4);

extern "C" bool osp_call_float(std::uintptr_t, std::uintptr_t, std::uintptr_t, float*);
extern "C" bool osp_call_word(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uint64_t*);
extern "C" bool osp_call_void2(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

namespace {
using osp::Json;
using osp::layout::Address;
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value) : h(value) {}
    ~Handle() { if (h!=INVALID_HANDLE_VALUE && h) CloseHandle(h); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    bool valid() const { return h && h!=INVALID_HANDLE_VALUE; }
    bool close() { auto value=h; h=INVALID_HANDLE_VALUE; return CloseHandle(value)!=0; }
};
struct Reader {
    bool read(Address p, void* out, std::size_t n) {
        if (!osp::address_range(p,n)) return false;
        SIZE_T got=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,n,&got) && got==n;
    }
};
struct State {
    std::wstring directory, log_path, settings_path;
    std::string session;
    Address image=0;
    UInt32 runtime=0, f4se=0, release=0;
    LONG sequence=0, busy=0;
    LONG node_sequence=0,scene_sequence=0,scene3_sequence=0;
    osp::scene3::Tracker scene3_validation;
    LARGE_INTEGER node_clock_origin{},node_clock_frequency{};
    PluginHandle handle=kPluginHandle_Invalid;
    bool console_registered=false;
};
// No application work or dynamically constructed objects in DllMain.
State* state=nullptr; // Lifetime is the game process; never unloaded after registration.

std::string utc_now() {
    SYSTEMTIME t{}; GetSystemTime(&t);
    char s[40];
    std::snprintf(s,sizeof(s),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                  t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return s;
}
std::string utf8(const wchar_t* s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,nullptr,0,nullptr,nullptr);
    if (n<=0) throw std::runtime_error("Unicode path conversion failed");
    std::string out(std::size_t(n),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,out.data(),n,nullptr,nullptr))
        throw std::runtime_error("Unicode path conversion failed");
    out.pop_back(); return out;
}
void log_line(const std::string& message) noexcept {
    try {
        auto line=utc_now()+" "+message+"\r\n";
        for (std::size_t i=0;i+2<line.size();++i)
            if (line[i]=='\r' || line[i]=='\n') line[i]=' ';
        if (!state || state->log_path.empty()) { OutputDebugStringA(line.c_str()); return; }
        Handle f(CreateFileW(state->log_path.c_str(),FILE_APPEND_DATA,
                 FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        DWORD written=0;
        if (!f.valid() || !WriteFile(f.h,line.data(),DWORD(line.size()),&written,nullptr) || written!=line.size())
            OutputDebugStringA(line.c_str());
    } catch (...) { OutputDebugStringA("osp_measure: logging failed\n"); }
}
void error(Json::Array& errors, const std::string& text) {
    errors.emplace_back(text); log_line("ERROR "+text);
}
template<class T> std::optional<T> read(Reader& r,Address p) { return osp::read_at<Reader,T>(r,p); }
std::uintptr_t method(Reader& r, Address self, std::size_t slot) {
    auto table=read<Address>(r,self);
    if (!table || !*table || *table>UINT64_MAX-slot*8) return 0;
    auto fn=read<Address>(r,*table+slot*8);
    if (!fn || !*fn) return 0;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(*fn),&m,sizeof(m)) || m.State!=MEM_COMMIT ||
        (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return 0;
    if (!(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return 0;
    return *fn;
}
std::optional<double> actor_value(Reader& r,Address player,Address av,std::size_t slot) {
    if (!player || !av || player>UINT64_MAX-0x58) return std::nullopt;
    Address self=player+0x58;
    auto fn=method(r,self,slot); float value=0;
    if (!fn || !osp_call_float(fn,self,av,&value) || !std::isfinite(value)) return std::nullopt;
    return double(value);
}
Json maybe_number(std::optional<double> n) { return n ? Json(*n) : Json(); }
void capture(osp::Trigger why,const std::string& tag="",bool light=false);
bool console_snapshot(void*,void*,void*,void*,void*,void*,double* result,void*) noexcept;
bool console_dispatch(void*,void*,void*,void*,void*,void*,double*,void*) noexcept;
void capture_nodes(const osp::nodes::Request&);
void capture_scene(const osp::nodes::Request&);
void capture_scene3(const osp::scene3::Request&);
bool sequencer_armed() noexcept;
bool sequencer_mark(double* result) noexcept;
void sequencer_message(std::uint32_t type) noexcept;

Json console_catalog(Reader& reader) {
    Json::Array names;
    for (auto table:{std::pair<Address,std::size_t>{osp::layout::console_commands_rva,osp::layout::console_command_count},
                    {osp::layout::script_commands_rva,osp::layout::script_command_count}}) {
        for (std::size_t i=0;i<table.second;++i) {
            auto cmd=read<osp::layout::ConsoleCommand>(reader,state->image+table.first+i*0x50);
            if (!cmd) continue;
            for (Address pointer:{cmd->long_name,cmd->short_name}) {
                // Preserve the v5 catalogue byte stream: the new alias is
                // described in node-dump.md, not added to legacy payloads.
                if (pointer==cmd->short_name && cmd->execute==reinterpret_cast<Address>(&console_dispatch)) continue;
                auto name=osp::read_string(reader,pointer,256);
                if (name && !name->empty()) names.emplace_back(*name);
            }
        }
    }
    return names;
}
void register_console() {
    if (state->console_registered) return;
    Reader reader;
    // The SDK explicitly names this unused candidate; ForceRSXCrash is already
    // used by F4SE itself. Match its current name so other plugins are respected.
    for (std::size_t i=0;i<osp::layout::console_command_count;++i) {
        Address slot=state->image+osp::layout::console_commands_rva+i*0x50;
        auto original=read<osp::layout::ConsoleCommand>(reader,slot);
        if (!original || original->opcode<0x100 || original->opcode>=0x309) continue;
        auto name=osp::read_string(reader,original->long_name,256);
        if (!name || *name!="ToggleESRAM") continue;
        static constexpr char command_name[]="ospsnap",alias[]="ospnodes";
        static constexpr char help[]="Write an osp_measure snapshot; optional tag comes from the Documents control file.";
        auto replacement=osp::snapshot_command(*original,reinterpret_cast<Address>(command_name),
            reinterpret_cast<Address>(alias),reinterpret_cast<Address>(help),reinterpret_cast<Address>(&console_dispatch));
        DWORD protection=0; SIZE_T written=0;
        if (!VirtualProtect(reinterpret_cast<void*>(slot),sizeof(replacement),PAGE_READWRITE,&protection)) {
            log_line("ERROR cannot make console command record writable"); return;
        }
        bool ok=WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(slot),&replacement,sizeof(replacement),&written)
            && written==sizeof(replacement);
        if (!ok) {
            SIZE_T restored=0;
            WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(slot),&*original,sizeof(*original),&restored);
        }
        DWORD ignored=0;
        if (!VirtualProtect(reinterpret_cast<void*>(slot),sizeof(replacement),protection,&ignored))
            log_line("ERROR cannot restore console table protection");
        state->console_registered=ok;
        log_line(ok?"registered ospsnap (zero console arguments; tag sidecar supported)":"ERROR console command write failed");
        return;
    }
    log_line("ERROR unused console slot ToggleESRAM unavailable; ospsnap not registered");
}
std::optional<std::string> control_text() {
    auto path=state->directory+L"\\osp-measure\\control.tag";
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                         nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (!f.valid()) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return std::string();
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h,&size) || size.QuadPart<0 || size.QuadPart>192) return std::nullopt;
    std::string text(std::size_t(size.QuadPart),'\0'); DWORD got=0;
    if (!text.empty() && (!ReadFile(f.h,text.data(),DWORD(text.size()),&got,nullptr) || got!=text.size())) return std::nullopt;
    return text;
}
std::optional<osp::CaptureRequest> control_tag() {
    auto text=control_text();
    return text?osp::snapshot_control(std::move(*text)):std::nullopt;
}
bool console_dispatch(void* a,void* b,void* c,void* d,void* e,void* f,double* result,void* h) noexcept {
    if (!state) return true;
    // v9: an installed packet turns ospsnap into the sequencer's step mark.
    if (sequencer_armed()) return sequencer_mark(result);
    try {
        auto text=control_text();
        auto third=text?osp::scene3::request(*text):std::nullopt;
        auto extended=text?osp::scene::request(*text):std::nullopt;
        auto request=third?std::optional<osp::nodes::Request>(third->base):extended?extended:(text?osp::nodes::request(*text):std::nullopt);
        if (!request) return console_snapshot(a,b,c,d,e,f,result,h);
        if (InterlockedCompareExchange(&state->busy,1,0)!=0) {log_line("ERROR reentrant node dump skipped");return true;}
        struct Reset {~Reset(){InterlockedExchange(&state->busy,0);}} reset;
        if(third) capture_scene3(*third); else if(extended) capture_scene(*request); else capture_nodes(*request);
    } catch(const std::exception& ex) {log_line(std::string("ERROR node dump: ")+ex.what());}
      catch(...) {log_line("ERROR node dump failed");}
    // Node capture does not write even the console result pointer.
    return true;
}
bool console_snapshot(void*,void*,void*,void*,void*,void*,double* result,void*) noexcept {
    if (!state) return true;
    if (result) { double zero=0; SIZE_T done=0; WriteProcessMemory(GetCurrentProcess(),result,&zero,sizeof(zero),&done); }
    try {
        if (InterlockedCompareExchange(&state->busy,1,0)!=0) { log_line("ERROR reentrant console capture skipped"); return true; }
        struct Reset { ~Reset() { InterlockedExchange(&state->busy,0); } } reset;
        auto tag=control_tag();
        if (!tag) { log_line("ERROR invalid snapshot tag control file"); return true; }
        capture(osp::Trigger::console,tag->tag,tag->light);
        if (result) { double one=1; SIZE_T done=0; WriteProcessMemory(GetCurrentProcess(),result,&one,sizeof(one),&done); }
    } catch (const std::exception& e) { log_line(std::string("ERROR console capture: ")+e.what()); }
      catch (...) { log_line("ERROR console capture failed"); }
    return true;
}

std::wstring settings_file_path() {
    HMODULE module=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&state),&module))
        throw std::runtime_error("cannot resolve plugin module");
    std::vector<wchar_t> path(32768);
    DWORD n=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
    if (!n || n>=path.size()) throw std::runtime_error("cannot resolve plugin DLL path");
    std::wstring value(path.data(),n);
    auto slash=value.find_last_of(L"\\/");
    if (slash==std::wstring::npos) throw std::runtime_error("plugin DLL path has no directory");
    return value.substr(0,slash+1)+L"osp_measure_settings.txt";
}
osp::SettingsNames settings_names() {
    osp::SettingsNames failed;
    Handle f(CreateFileW(state->settings_path.c_str(),GENERIC_READ,FILE_SHARE_READ,
                         nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (!f.valid()) {
        failed.errors.push_back("cannot open settings list, win32="+std::to_string(GetLastError())); return failed;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h,&size) || size.QuadPart<0 || size.QuadPart>std::int64_t(osp::max_names_bytes)) {
        failed.errors.push_back("cannot size settings list or list exceeds 4 MiB"); return failed;
    }
    std::string bytes(std::size_t(size.QuadPart),'\0'); std::size_t offset=0;
    while (offset<bytes.size()) {
        DWORD got=0;
        if (!ReadFile(f.h,bytes.data()+offset,DWORD(bytes.size()-offset),&got,nullptr) || !got) {
            failed.errors.push_back("settings list read failed or file truncated"); return failed;
        }
        offset+=got;
    }
    return osp::parse_settings_names(std::move(bytes));
}
struct SettingCalls {
    bool construct(osp::layout::FixedString* key,const char* text) noexcept {
        return osp_call_void2(state->image+osp::layout::fixed_string_ctor_rva,
            reinterpret_cast<Address>(key),reinterpret_cast<Address>(text),0);
    }
    bool find(Address map,osp::layout::FixedString* key,Address** output) noexcept {
        return osp_call_void2(state->image+osp::layout::setting_lookup_rva,map,
            reinterpret_cast<Address>(key),reinterpret_cast<Address>(output));
    }
    bool release(osp::layout::FixedString* key) noexcept {
        return osp_call_void2(state->image+osp::layout::fixed_string_release_rva,
            reinterpret_cast<Address>(key),0,0);
    }
};
struct SettingsSnapshot { Json json; bool complete; };
SettingsSnapshot settings_snapshot(Reader& reader,Json::Array& errors) {
    auto names=settings_names(); Json::Array input_errors,items;
    for (const auto& e:names.errors) { input_errors.emplace_back(e); error(errors,e); }
    auto singleton=read<Address>(reader,state->image+osp::layout::game_settings_rva);
    auto collection=singleton ? read<osp::layout::SettingCollection>(reader,*singleton) : std::nullopt;
    Address map=collection ? collection->map : 0;
    SettingCalls calls;
    std::size_t queried=0,found=0,values=0,item_errors=0;
    for (const auto& name:names.names) {
        auto item=osp::observe_setting(reader,name,osp::lookup_setting(calls,map,name));
        queried+=item.queried; found+=item.found; values+=item.value_read;
        if (!item.errors.empty()) ++item_errors;
        for (const auto& e:item.errors) error(errors,"GameSetting "+name+": "+e);
        items.push_back(std::move(item.json));
    }
    bool complete=osp::settings_coverage_complete(names,queried);
    bool all_values=complete && found==names.names.size() && values==found && item_errors==0;
    return {Json::object({{"complete",complete},{"values_complete",all_values},
        {"coverage","configured_names"},{"all_engine_settings",false},
        {"status",complete?"list_queried":"incomplete"},
        {"source_file",utf8(state->settings_path.c_str())},{"requested_count",names.names.size()},
        {"queried_count",queried},{"found_count",found},{"items_with_errors",item_errors},
        {"duplicate_names_ignored",names.duplicates},{"input_errors",input_errors},{"items",items}}),complete};
}

Json mod_list(Reader& r,const osp::layout::DataHandler& dh,Json::Array& errors) {
    Json::Array items; bool complete=true; std::set<Address> seen;
    for (const auto& a : {dh.full_mods,dh.light_mods}) {
        if (!osp::valid_array(a)) { error(errors,"invalid loaded mod array"); complete=false; continue; }
        for (UInt32 i=0;i<a.count;++i) {
            auto ptr=read<Address>(r,a.entries+std::size_t(i)*8);
            if (ptr && *ptr && !seen.insert(*ptr).second) continue;
            auto mod=ptr ? read<osp::layout::Mod>(r,*ptr) : std::nullopt;
            if (!mod) {
                complete=false; error(errors,"unreadable loaded mod at array index "+std::to_string(i));
                items.emplace_back(Json::object({{"name",nullptr},{"status","unreadable"}})); continue;
            }
            const auto end=std::find(std::begin(mod->name),std::end(mod->name),'\0');
            if (end==std::end(mod->name)) {
                complete=false; error(errors,"unterminated loaded mod name");
            }
            bool light=(mod->record_flags & (1u<<9))!=0;
            items.emplace_back(Json::object({{"name",std::string(mod->name,end)},
                {"kind",light?"light":"full"},{"mod_index",mod->index},
                {"light_index",light?Json(mod->light_index):Json()},
                {"record_flags_hex",osp::hex(mod->record_flags)}}));
        }
    }
    return Json::object({{"complete",complete},{"items",items}});
}
Json native_modules(Json::Array& errors) {
    Json::Array items; bool complete=true;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId()));
    if (!snapshot.valid()) { error(errors,"cannot enumerate native modules"); complete=false; }
    else {
        MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
        if (!Module32FirstW(snapshot.h,&entry)) { error(errors,"Module32FirstW failed"); complete=false; }
        else {
            do {
                // GetProcAddress does not call these exports or load a module.
                bool query=GetProcAddress(entry.hModule,"F4SEPlugin_Query")!=nullptr;
                bool load=GetProcAddress(entry.hModule,"F4SEPlugin_Load")!=nullptr;
                if (query || load) items.emplace_back(Json::object({
                    {"module",utf8(entry.szModule)},{"path",utf8(entry.szExePath)},
                    {"query_export",query},{"load_export",load}}));
            } while (Module32NextW(snapshot.h,&entry));
            if (GetLastError()!=ERROR_NO_MORE_FILES) {
                error(errors,"native module enumeration ended with an error"); complete=false;
            }
        }
    }
    return Json::object({{"module_enumeration_complete",complete},
        {"registration_status","not_enumerable_by_public_f4se_api"},{"items",items}});
}
Json player_snapshot(Reader& r,Address player,const osp::layout::DataHandler* dh,Json::Array& errors) {
    Json::Array values; bool complete=true; Json level;
    auto ref=read<osp::layout::Reference>(r,player);
    if (!ref) { error(errors,"player is null or unreadable"); complete=false; }
    else {
        auto base=read<osp::layout::Form>(r,ref->base_form);
        auto base_data=read<osp::layout::ActorBase>(r,ref->base_form);
        std::uint64_t n=0;
        if (!base || base->type!=45 || !base_data ||
            !osp_call_word(state->image+osp::layout::actor_base_level_rva,
                ref->base_form+offsetof(osp::layout::ActorBase,actor_data),0,&n)) {
            error(errors,"cannot read player level via TESActorBaseData::GetLevel"); complete=false;
        } else level=std::uint16_t(n);
    }
    if (!dh || !osp::valid_array(dh->forms[98])) {
        error(errors,"ActorValue array unavailable or invalid"); complete=false;
    } else {
        const auto& a=dh->forms[98];
        for (UInt32 i=0;i<a.count;++i) {
            auto ptr=read<Address>(r,a.entries+std::size_t(i)*8);
            auto form=ptr ? read<osp::layout::Form>(r,*ptr) : std::nullopt;
            Json id,editor; std::optional<double> current,base,maximum;
            if (!ptr || !*ptr || !form || form->type!=98) {
                error(errors,"invalid ActorValue at array index "+std::to_string(i)); complete=false;
            } else {
                id=osp::hex(form->id);
                auto fn=method(r,*ptr,osp::layout::editor_id_slot); std::uint64_t text=0;
                auto edid=fn && osp_call_word(fn,*ptr,0,&text) ? osp::read_string(r,text) : std::nullopt;
                if (edid) editor=*edid;
                else { error(errors,"EditorID unavailable for ActorValue "+osp::hex(form->id)); complete=false; }
                if (ref) {
                    current=actor_value(r,player,*ptr,osp::layout::current_slot);
                    base=actor_value(r,player,*ptr,osp::layout::base_slot);
                    maximum=actor_value(r,player,*ptr,osp::layout::maximum_slot);
                }
                if (!current || !base || !maximum) {
                    error(errors,"base/current/maximum value unavailable or non-finite for ActorValue "+osp::hex(form->id));
                    complete=false;
                }
            }
            values.emplace_back(Json::object({{"array_index",i},{"editor_id",editor},
                {"form_id",id},{"base",maybe_number(base)},{"current",maybe_number(current)},
                {"maximum",maybe_number(maximum)}}));
        }
    }
    const auto previous_errors=errors.size();
    auto equipped=osp::weapon::observe(r,player,errors);
    for(std::size_t i=previous_errors;i<errors.size();++i)
        if(auto message=std::get_if<std::string>(&errors[i].data)) log_line("ERROR "+*message);
    return Json::object({{"complete",complete},{"level",level},{"equipped_weapon",equipped},
        {"level_source","player.baseForm.actorData.GetLevel"},{"actor_values",values}});
}
void atomic_write(const std::wstring& path,const std::string& content) {
    auto tmp=path+L".tmp";
    Handle file(CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (!file.valid()) throw std::runtime_error("cannot create JSON temporary file, win32="+std::to_string(GetLastError()));
    try {
        std::size_t offset=0;
        while (offset<content.size()) {
            DWORD n=DWORD(std::min<std::size_t>(content.size()-offset,1024*1024)),done=0;
            if (!WriteFile(file.h,content.data()+offset,n,&done,nullptr) || !done)
                throw std::runtime_error("JSON write failed, win32="+std::to_string(GetLastError()));
            offset+=done;
        }
        if (!FlushFileBuffers(file.h)) throw std::runtime_error("JSON flush failed");
        if (!file.close()) throw std::runtime_error("JSON close failed");
        // Same directory/volume; no replacement of any existing snapshot.
        if (!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("JSON rename failed, win32="+std::to_string(GetLastError()));
    } catch (...) {
        if (file.valid()) file.close();
        DeleteFileW(tmp.c_str()); throw;
    }
}
struct NodeCalls {
    Reader& reader;
    std::optional<Address> get(Address self,std::size_t slot,Address arg=0) {
        auto fn=method(reader,self,slot);std::uint64_t out=0;
        if(!fn || !osp_call_word(fn,self,arg,&out)) return std::nullopt;
        return out;
    }
};
void capture_nodes(const osp::nodes::Request& request) {
    Reader reader;NodeCalls calls{reader};Json::Array roots,errors;
    std::uint64_t reference=0;
    if(!osp_call_word(state->image+osp::nodes::lookup_form_rva,request.form_id,0,&reference))
        error(errors,"LookupFormByID foreign call failed");
    auto form=read<osp::layout::Form>(reader,reference);
    bool valid=form && form->id==request.form_id && (form->type==64 || form->type==65);
    auto base=valid?osp::nodes::field<Address>(reader,reference,0xe0):std::nullopt;
    auto base_form=base?read<osp::layout::Form>(reader,*base):std::nullopt;
    if(valid && !base_form) error(errors,"Base FormID unavailable");
    if(!valid) error(errors,"Requested FormID is not a readable REFR/ACHR");
    else {
        auto append=[&](const char* view,std::size_t slot,Address arg) {
            auto root=calls.get(reference,slot,arg);
            if(!root) error(errors,std::string(view)+" root getter failed");
            auto tree=osp::nodes::tree(reader,calls,root.value_or(0));
            for(const auto& field:std::get<Json::Object>(tree.data))
                if(field.first=="stops") for(const auto& stop:std::get<Json::Array>(field.second.data))
                    log_line(std::string("ERROR node ")+view+" "+osp::encode(stop));
            roots.push_back(Json::object({{"view",view},{"getter_slot",slot},
                {"tree",std::move(tree)}}));
        };
        if(request.form_id==0x14) {
            append("third_person",osp::nodes::actor_root_slot,0);
            append("first_person",osp::nodes::actor_root_slot,1);
        } else append("object",osp::nodes::object_root_slot,0);
    }
    LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now)) error(errors,"QueryPerformanceCounter failed");
    const auto sequence=InterlockedIncrement(&state->node_sequence);
    bool complete=valid && base_form && errors.empty();
    for(const auto& root:roots) {
        const auto& obj=std::get<Json::Object>(root.data);
        complete=complete && osp::flag(obj.back().second,"complete");
    }
    Json output=Json::object({{"node_dump_schema",1},{"plugin_version",6},
        {"session_id",state->session},{"tag",request.tag},{"requested_form_id",osp::hex(request.form_id)},
        {"reference_form_id",form?Json(osp::hex(form->id)):Json()},
        {"base_form_id",base_form?Json(osp::hex(base_form->id)):Json()},
        {"dump_counter",sequence},{"monotonic_ticks",now.QuadPart-state->node_clock_origin.QuadPart},
        {"monotonic_frequency",state->node_clock_frequency.QuadPart},
        {"clock_origin","QueryPerformanceCounter at plugin load; process-local elapsed ticks"},
        {"windows_process_id",GetCurrentProcessId()},{"complete",complete},{"roots",roots},{"errors",errors}});
    auto filename=state->session+"-nodes-"+std::to_string(sequence)+".json";
    atomic_write(state->directory+L"\\osp-measure\\"+std::wstring(filename.begin(),filename.end()),osp::encode(output));
    log_line("wrote "+filename+(complete?" (node dump)":" (incomplete node dump)"));
}
Json window_snapshot() {
    auto handle=GetForegroundWindow();DWORD pid=0;
    if(handle) GetWindowThreadProcessId(handle,&pid);
    RECT client{},bounds{};POINT origin{};
    bool valid=handle && pid==GetCurrentProcessId() && GetClientRect(handle,&client) &&
        GetWindowRect(handle,&bounds) && ClientToScreen(handle,&origin);
    auto rectangle=[](const RECT& r) {return Json::Array{Json(r.left),Json(r.top),Json(r.right),Json(r.bottom)};};
    return Json::object({{"complete",valid},{"own_foreground_window",pid==GetCurrentProcessId()},
        {"client_rect",valid?Json(rectangle(client)):Json()},{"window_rect",valid?Json(rectangle(bounds)):Json()},
        {"client_screen_origin",valid?Json(Json::Array{Json(origin.x),Json(origin.y)}):Json()},
        {"coordinate_space","Win32 device coordinates; verify Wine-to-Mac screenshot mapping independently"}});
}
void capture_scene(const osp::nodes::Request& request) {
    Reader reader;NodeCalls calls{reader};Json::Array roots,errors;
    std::uint64_t reference=0;
    if(!osp_call_word(state->image+osp::nodes::lookup_form_rva,request.form_id,0,&reference))
        error(errors,"LookupFormByID foreign call failed");
    auto form=read<osp::layout::Form>(reader,reference);
    bool valid=form && form->id==request.form_id && (form->type==64 || form->type==65);
    auto base=valid?osp::nodes::field<Address>(reader,reference,0xe0):std::nullopt;
    auto base_form=base?read<osp::layout::Form>(reader,*base):std::nullopt;
    if(valid && !base_form) error(errors,"Base FormID unavailable");
    if(!valid) error(errors,"Requested FormID is not a readable REFR/ACHR");
    else {
        auto append=[&](const char* view,std::size_t slot,Address arg) {
            auto root=calls.get(reference,slot,arg);
            if(!root) error(errors,std::string(view)+" root getter failed");
            auto tree=osp::nodes::tree(reader,calls,root.value_or(0));
            for(const auto& field:std::get<Json::Object>(tree.data))
                if(field.first=="stops") for(const auto& stop:std::get<Json::Array>(field.second.data))
                    log_line(std::string("ERROR node ")+view+" "+osp::encode(stop));
            auto flat=osp::scene::flattened_trees(reader,tree);
            for(const auto& candidate:std::get<Json::Array>(flat.data)) {
                auto failures=osp::scene::member(candidate,"errors");
                if(failures) for(const auto& failure:std::get<Json::Array>(failures->data))
                    log_line(std::string("ERROR flattened ")+view+" "+osp::encode(failure));
            }
            roots.push_back(Json::object({{"view",view},{"getter_slot",slot},
                {"tree",std::move(tree)},{"flattened_bone_trees",std::move(flat)}}));
        };
        if(request.form_id==0x14) {
            append("third_person",osp::nodes::actor_root_slot,0);
            append("first_person",osp::nodes::actor_root_slot,1);
        } else append("object",osp::nodes::object_root_slot,0);
    }
    LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now)) error(errors,"QueryPerformanceCounter failed");
    const auto sequence=InterlockedIncrement(&state->scene_sequence);
    bool complete=valid && base_form && errors.empty();
    for(const auto& root:roots) {
        const auto& obj=std::get<Json::Object>(root.data);
        auto tree=osp::scene::member(root,"tree");
        complete=complete && tree && osp::flag(*tree,"complete");
        auto flat=osp::scene::member(root,"flattened_bone_trees");
        if(flat) for(const auto& candidate:std::get<Json::Array>(flat->data)) complete=complete && osp::flag(candidate,"complete");
        (void)obj;
    }
    auto camera=osp::scene::camera(reader,calls,state->image);
    if(auto failures=osp::scene::member(camera,"errors"))
        for(const auto& failure:std::get<Json::Array>(failures->data)) log_line("ERROR camera "+osp::encode(failure));
    complete=complete && osp::flag(camera,"complete");
    Json output=Json::object({{"node_dump_schema",2},{"plugin_version",7},
        {"session_id",state->session},{"tag",request.tag},{"requested_form_id",osp::hex(request.form_id)},
        {"reference_form_id",form?Json(osp::hex(form->id)):Json()},
        {"base_form_id",base_form?Json(osp::hex(base_form->id)):Json()},
        {"dump_counter",sequence},{"monotonic_ticks",now.QuadPart-state->node_clock_origin.QuadPart},
        {"monotonic_frequency",state->node_clock_frequency.QuadPart},
        {"clock_origin","QueryPerformanceCounter at plugin load; process-local elapsed ticks"},
        {"windows_process_id",GetCurrentProcessId()},{"complete",complete},{"roots",roots},{"camera",camera},
        {"flattened_layout_validation","requires_four_external_proofs"},{"window",window_snapshot()},{"errors",errors}});
    auto filename=state->session+"-scene-"+std::to_string(sequence)+".json";
    atomic_write(state->directory+L"\\osp-measure\\"+std::wstring(filename.begin(),filename.end()),osp::encode(output));
    log_line("wrote "+filename+(complete?" (node dump)":" (incomplete node dump)"));
}
osp::scene3::Forecast scene3_forecast() {
    const auto path=state->directory+L"\\osp-measure\\bones19.bin";
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file.valid()) {osp::scene3::Forecast f;f.error="cannot_open_bones19.bin";return f;}
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.h,&size) || size.QuadPart<0 || size.QuadPart>512*1024) {osp::scene3::Forecast f;f.error="forecast_size_limit";return f;}
    std::string bytes(std::size_t(size.QuadPart),'\0');DWORD got=0;
    if(!bytes.empty() && (!ReadFile(file.h,bytes.data(),DWORD(bytes.size()),&got,nullptr) || got!=bytes.size())) {osp::scene3::Forecast f;f.error="forecast_read_failed";return f;}
    return osp::scene3::forecast(bytes);
}
void capture_scene3(const osp::scene3::Request& extended) {
    const auto& request=extended.base;auto prediction=scene3_forecast();
    bool proof_pass=prediction.complete;std::size_t applicable=0;
    Reader reader;NodeCalls calls{reader};SettingCalls string_calls;Json::Array roots,errors;
    std::uint64_t reference=0;
    if(!osp_call_word(state->image+osp::nodes::lookup_form_rva,request.form_id,0,&reference))
        error(errors,"LookupFormByID foreign call failed");
    auto form=read<osp::layout::Form>(reader,reference);
    bool valid=form && form->id==request.form_id && (form->type==64 || form->type==65);
    auto base=valid?osp::nodes::field<Address>(reader,reference,0xe0):std::nullopt;
    auto base_form=base?read<osp::layout::Form>(reader,*base):std::nullopt;
    if(valid && !base_form) error(errors,"Base FormID unavailable");
    if(!valid) error(errors,"Requested FormID is not a readable REFR/ACHR");
    else {
        auto append=[&](const char* view,std::size_t slot,Address arg) {
            auto root=calls.get(reference,slot,arg);
            if(!root) error(errors,std::string(view)+" root getter failed");
            auto tree=osp::nodes::tree(reader,calls,root.value_or(0));
            for(const auto& field:std::get<Json::Object>(tree.data))
                if(field.first=="stops") for(const auto& stop:std::get<Json::Array>(field.second.data))
                    log_line(std::string("ERROR node ")+view+" "+osp::encode(stop));
            auto flat=osp::scene::flattened_trees(reader,tree);
            auto& candidates=std::get<Json::Array>(flat.data);
            if(request.form_id==0x14 && candidates.size()!=1)proof_pass=false;
            for(auto& candidate:candidates) {
                ++applicable;const auto& expected=std::string(view)=="first_person"?prediction.first:prediction.third;
                auto proof=osp::scene3::enrich(reader,string_calls,candidate,tree,expected);
                proof_pass=proof_pass && proof.accepted();
                if(!proof.accepted())log_line(std::string("ERROR round19 proof failed: ")+view);
                auto failures=osp::scene::member(candidate,"errors");
                if(failures) for(const auto& failure:std::get<Json::Array>(failures->data))
                    log_line(std::string("ERROR flattened ")+view+" "+osp::encode(failure));
            }
            roots.push_back(Json::object({{"view",view},{"getter_slot",slot},
                {"tree",std::move(tree)},{"flattened_bone_trees",std::move(flat)}}));
        };
        if(request.form_id==0x14) {
            append("third_person",osp::nodes::actor_root_slot,0);
            append("first_person",osp::nodes::actor_root_slot,1);
        } else append("object",osp::nodes::object_root_slot,0);
    }
    LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now)) error(errors,"QueryPerformanceCounter failed");
    const auto sequence=InterlockedIncrement(&state->scene3_sequence);
    bool complete=valid && base_form && errors.empty();
    for(const auto& root:roots) {
        const auto& obj=std::get<Json::Object>(root.data);
        auto tree=osp::scene::member(root,"tree");
        complete=complete && tree && osp::flag(*tree,"complete");
        auto flat=osp::scene::member(root,"flattened_bone_trees");
        if(flat) for(const auto& candidate:std::get<Json::Array>(flat->data)) complete=complete && osp::flag(candidate,"complete");
        (void)obj;
    }
    auto camera=osp::scene::camera(reader,calls,state->image);
    if(auto failures=osp::scene::member(camera,"errors"))
        for(const auto& failure:std::get<Json::Array>(failures->data)) log_line("ERROR camera "+osp::encode(failure));
    complete=complete && osp::flag(camera,"complete");
    bool validated=state->scene3_validation.observe(prediction.id,extended.pause,request.form_id==0x14,applicable!=0,proof_pass);
    const char* validation=validated?"validated_round19":"candidate_round19";
    for(auto& root:roots)if(auto flats=osp::field(root,"flattened_bone_trees"))
        for(auto& flat:std::get<Json::Array>(flats->data))if(auto status=osp::field(flat,"validation_status"))*status=Json(validation);
    Json::Array accepted_pauses;for(auto p:state->scene3_validation.pauses)accepted_pauses.emplace_back(p);
    auto raw_vector=[&](std::size_t offset) {Json::Array out;auto a=osp::nodes::field<std::array<std::uint32_t,3>>(reader,reference,offset);if(a)for(auto w:*a)out.push_back(osp::nodes::scalar(w));return a?Json(out):Json();};
    auto cell=osp::nodes::field<Address>(reader,reference,0xb8);auto cell_form=cell?read<osp::layout::Form>(reader,*cell):std::nullopt;
    Json reference_pose=Json::object({{"sdk_rot_raw",raw_vector(0xc0)},{"position_raw",raw_vector(0xd0)},
        {"parent_cell_form_id",cell_form?Json(osp::hex(cell_form->id)):Json()},
        {"parent_cell_type_raw",cell_form?Json(cell_form->type):Json()},
        {"rot_semantics","SDK names rot and comments Probably quat?; no angle-unit interpretation asserted"}});
    if(!prediction.complete) {complete=false;error(errors,"Round19 forecast unavailable: "+prediction.error);}
    Json output=Json::object({{"node_dump_schema",3},{"plugin_version",8},
        {"session_id",state->session},{"tag",request.tag},{"requested_form_id",osp::hex(request.form_id)},
        {"reference_form_id",form?Json(osp::hex(form->id)):Json()},
        {"base_form_id",base_form?Json(osp::hex(base_form->id)):Json()},
        {"dump_counter",sequence},{"monotonic_ticks",now.QuadPart-state->node_clock_origin.QuadPart},
        {"monotonic_frequency",state->node_clock_frequency.QuadPart},
        {"clock_origin","QueryPerformanceCounter at plugin load; process-local elapsed ticks"},
        {"windows_process_id",GetCurrentProcessId()},{"complete",complete},{"roots",roots},{"camera",camera},
        {"flattened_layout_validation",validation},{"reference_sdk_pose",reference_pose},
        {"round19_validation",Json::object({{"forecast_fnv1a64",prediction.id},{"forecast_complete",prediction.complete},
            {"current_proofs_passed",proof_pass},{"applicable_trees",applicable},{"pause_id",extended.pause},
            {"accepted_player_pauses",accepted_pauses},{"required_distinct_player_pauses",2}})},{"window",window_snapshot()},{"errors",errors}});
    auto filename=state->session+"-scene3-"+std::to_string(sequence)+".json";
    atomic_write(state->directory+L"\\osp-measure\\"+std::wstring(filename.begin(),filename.end()),osp::encode(output));
    log_line("wrote "+filename+(complete?" (node dump)":" (incomplete node dump)"));
}
void capture(osp::Trigger why,const std::string& tag,bool light) {
    Json::Array errors; Reader reader;
    auto data=read<Address>(reader,state->image+osp::layout::data_handler_rva);
    auto dh=data ? read<osp::layout::DataHandler>(reader,*data) : std::nullopt;
    auto player=read<Address>(reader,state->image+osp::layout::player_rva);
    Json mods=light?osp::omitted_section():Json::object({{"complete",false},{"items",Json::Array{}}});
    if (!dh) error(errors,"DataHandler is null or unreadable");
    else if (!light) mods=mod_list(reader,*dh,errors);
    auto native=light?osp::omitted_section():native_modules(errors);
    auto pc=player_snapshot(reader,player.value_or(0),dh?&*dh:nullptr,errors);
    auto settings=light?SettingsSnapshot{osp::omitted_section(),true}:settings_snapshot(reader,errors);
    bool complete=osp::flag(pc,"complete") && (light ||
        (settings.complete && osp::flag(mods,"complete") && osp::flag(native,"module_enumeration_complete")));
    const auto sequence=InterlockedIncrement(&state->sequence);
    const auto timestamp=utc_now();
    Json root=Json::object({{"schema_version",4},{"plugin_version",5},{"complete",complete},{"tag",tag},
        {"complete_scope","legacy_schema4_sections; equipped_weapon has independent completeness"},
        {"capture_mode",light?"light":"full"},
        {"tick_ms",GetTickCount64()},{"windows_process_id",GetCurrentProcessId()},
        {"console_commands",light?Json():console_catalog(reader)},
        {"capabilities",Json::object({{"ospsnap",state->console_registered},
            {"console_tag_argument",false},{"tag_control_file",true},{"light_capture",true},
            {"native_command_runner",false},{"hit_events",false}})},
        {"game_settings_coverage",light?"omitted_by_request":"configured_names"},
        {"timestamp_utc",timestamp},{"session_id",state->session},{"sequence",sequence},
        {"event",why==osp::Trigger::loaded?"post_load_game":why==osp::Trigger::console?"console_snapshot":"new_game"},
        {"event_phase",why==osp::Trigger::loaded?"load_routine_returned":why==osp::Trigger::console?"console_handler":"new_game_created_before_game_loaded"},
        {"game",Json::object({{"version",osp::packed_version(state->runtime)},
            {"runtime_hex",osp::hex(state->runtime)}})},
        {"f4se",Json::object({{"version","0.6.23"},{"packed_hex",osp::hex(state->f4se)},
            {"release_index",state->release}})},
        {"plugins",mods},{"native_plugin_modules",native},
        {"game_settings",settings.json},
        {"player",pc},{"errors",errors},
        {"string_encoding","valid UTF-8 preserved; invalid bytes escaped as U+00xx"}});
    const auto filename=state->session+"-"+std::to_string(sequence)+".json";
    std::wstring wide(filename.begin(),filename.end());
    atomic_write(state->directory+L"\\osp-measure\\"+wide,osp::encode(root));
    log_line("wrote "+filename+(light?" (light capture)":complete?" (configured-name coverage complete)":" (incomplete: see errors)"));
}
// ------------------------------------------------------------------------
// Plugin v9 stepped sequencer (docs/sequencer.md). Active only while
// Data\F4SE\Plugins\osp_measure\packet\plan.txt exists; then ospsnap is the
// step mark and legacy automatic snapshots are suppressed. Every write goes
// through osp::seq::child under Data\F4SE\Plugins\osp_measure\runs.
namespace layout9=osp::layout9;
struct ShotKey { std::uint64_t size=0, time=0; };
struct Sequencer {
    std::wstring game_dir, own_dir;
    std::unique_ptr<osp::seq::Run> run;
    std::string run_id, plan_sha256, started;
    Json environment, preflight;
    Json::Array marks;
    std::map<std::string,ShotKey> seen;
    std::optional<std::uint8_t> bench_index;
    bool cell_confirmed=false, folder_ready=false;
    LONG pending_abort=0; std::string pending_reason;
    Json::Array f4se_messages;           // every F4SE message while a packet is installed, bounded
    LONG messages_unrecorded=0;          // arrived while a mark held the busy flag, or after the bound
};
constexpr std::size_t max_f4se_messages=256;
Sequencer* sequencer=nullptr;
std::string ascii_of(const std::wstring& w) {
    std::string out; for (auto c:w) { if (c<32 || c>126) return std::string(); out.push_back(char(c)); } return out;
}
std::wstring wide_of(const std::string& s) { return std::wstring(s.begin(),s.end()); }
std::optional<std::wstring> module_directory(HMODULE module) {
    std::vector<wchar_t> path(32768);
    DWORD n=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
    if (!n || n>=path.size()) return std::nullopt;
    std::wstring value(path.data(),n);
    auto slash=value.find_last_of(L"\\/");
    if (slash==std::wstring::npos) return std::nullopt;
    return value.substr(0,slash);
}
std::optional<std::wstring> module_path(HMODULE module) {
    std::vector<wchar_t> path(32768);
    DWORD n=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
    if (!n || n>=path.size()) return std::nullopt;
    return std::wstring(path.data(),n);
}
HMODULE own_module() {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&sequencer),&module);
    return module;
}
// Read-only access; nothing in the game folder is ever opened for writing.
std::optional<std::string> read_small(const std::wstring& path,std::size_t limit) {
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (!f.valid()) return std::nullopt;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h,&size) || size.QuadPart<0 || std::uint64_t(size.QuadPart)>limit) return std::nullopt;
    std::string bytes(std::size_t(size.QuadPart),'\0'); std::size_t offset=0;
    while (offset<bytes.size()) {
        DWORD got=0;
        if (!ReadFile(f.h,bytes.data()+offset,DWORD(bytes.size()-offset),&got,nullptr) || !got) return std::nullopt;
        offset+=got;
    }
    return bytes;
}
std::optional<std::pair<std::uint64_t,std::string>> hash_file(const std::wstring& path,std::uint64_t limit,DWORD share=FILE_SHARE_READ|FILE_SHARE_WRITE) {
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,share,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (!f.valid()) return std::nullopt;
    osp::Sha256 h; std::vector<char> block(1<<20); std::uint64_t total=0;
    while (true) {
        DWORD got=0;
        if (!ReadFile(f.h,block.data(),DWORD(block.size()),&got,nullptr)) return std::nullopt;
        if (!got) break;
        total+=got; if (total>limit) return std::nullopt;
        h.update(block.data(),got);
    }
    return std::make_pair(total,h.hex());
}
Json file_json(const std::string& name,const std::optional<std::pair<std::uint64_t,std::string>>& h) {
    return h?Json::object({{"file",name},{"size",h->first},{"sha256",h->second}}):Json();
}
bool sequencer_paths() {
    if (sequencer) return true;
    auto plugin_dir=module_directory(own_module());
    auto game_dir=module_directory(nullptr);
    if (!plugin_dir || !game_dir) return false;
    auto s=std::make_unique<Sequencer>();
    s->own_dir=*plugin_dir+L"\\osp_measure"; s->game_dir=*game_dir;
    sequencer=s.release();
    return true;
}
bool sequencer_armed() noexcept {
    try {
        if (!sequencer_paths()) return false;
        auto attributes=GetFileAttributesW((sequencer->own_dir+L"\\packet\\plan.txt").c_str());
        return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    } catch (...) { return false; }
}
void console_print(const std::string& text) noexcept {
    try {
        Reader reader;
        auto manager=read<Address>(reader,state->image+layout9::console_manager_rva);
        if (!manager || !*manager) return;
        auto line=osp::seq::console_line(text);
        osp_call_void2(state->image+layout9::console_print_rva,*manager,reinterpret_cast<Address>(line.c_str()),0);
    } catch (...) {}
}
bool make_folder(const std::vector<std::string>& parts) {
    auto path=osp::seq::child(sequencer->own_dir,parts);
    if (!path) return false;
    return CreateDirectoryW(path->c_str(),nullptr) || GetLastError()==ERROR_ALREADY_EXISTS;
}
bool write_run_json() noexcept {
    try {
        auto& s=*sequencer;
        if (!s.run) return false;
        if (!s.folder_ready) s.folder_ready=make_folder({"runs"}) && make_folder({"runs",s.run_id});
        auto final_path=osp::seq::child(s.own_dir,{"runs",s.run_id,"run.json"});
        auto tmp=osp::seq::child(s.own_dir,{"runs",s.run_id,"run.json.tmp"});
        if (!s.folder_ready || !final_path || !tmp) return false;
        Json::Array messages=s.f4se_messages;
        if (s.messages_unrecorded) messages.push_back(Json::object({{"unrecorded",std::uint64_t(s.messages_unrecorded)},
            {"reason","arrived while a mark was running, or after the first 256 messages"}}));
        auto text=osp::encode(osp::seq::run_json(*s.run,s.plan_sha256,state->session,s.run_id,s.started,utc_now(),
                                                 s.environment,s.preflight,s.marks,messages));
        {
            Handle f(CreateFileW(tmp->c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            DWORD done=0;
            if (!f.valid() || !WriteFile(f.h,text.data(),DWORD(text.size()),&done,nullptr) || done!=text.size() ||
                !FlushFileBuffers(f.h) || !f.close()) { DeleteFileW(tmp->c_str()); return false; }
        }
        if (!MoveFileExW(tmp->c_str(),final_path->c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp->c_str()); return false;
        }
        return true;
    } catch (...) { return false; }
}
// Loaded full and light plugins in array order (GameData.h:105-111,294).
std::optional<std::vector<std::tuple<std::string,std::string,unsigned>>> loaded_plugins(Reader& r) {
    auto data=read<Address>(r,state->image+osp::layout::data_handler_rva);
    auto dh=data?read<osp::layout::DataHandler>(r,*data):std::nullopt;
    if (!dh) return std::nullopt;
    std::vector<std::tuple<std::string,std::string,unsigned>> out; std::set<Address> seen;
    for (const auto& a:{dh->full_mods,dh->light_mods}) {
        if (!osp::valid_array(a)) return std::nullopt;
        for (UInt32 i=0;i<a.count;++i) {
            auto ptr=read<Address>(r,a.entries+std::size_t(i)*8);
            if (ptr && *ptr && !seen.insert(*ptr).second) continue;
            auto mod=ptr?read<osp::layout::Mod>(r,*ptr):std::nullopt;
            if (!mod) return std::nullopt;
            auto end=std::find(std::begin(mod->name),std::end(mod->name),'\0');
            if (end==std::end(mod->name)) return std::nullopt;
            bool light=(mod->record_flags&(1u<<9))!=0;
            out.emplace_back(light?"light":"full",std::string(mod->name,end),light?unsigned(mod->light_index):unsigned(mod->index));
        }
    }
    return out;
}
// GameDaysPassed and TimeScale words from the DataHandler GLOB array.
void game_time(Reader& r,osp::seq::Observation& o) {
    auto data=read<Address>(r,state->image+osp::layout::data_handler_rva);
    auto dh=data?read<osp::layout::DataHandler>(r,*data):std::nullopt;
    if (!dh || !osp::valid_array(dh->forms[layout9::glob_array_index])) return;
    const auto& a=dh->forms[layout9::glob_array_index];
    for (UInt32 i=0;i<a.count && i<100000;++i) {
        auto ptr=read<Address>(r,a.entries+std::size_t(i)*8);
        auto form=ptr&&*ptr?read<osp::layout::Form>(r,*ptr):std::nullopt;
        if (!form || form->type!=layout9::glob_form_type) continue;
        auto name_ptr=osp::nodes::field<Address>(r,*ptr,layout9::global_editor_id);
        auto name=name_ptr&&*name_ptr?osp::read_string(r,*name_ptr,64):std::nullopt;
        if (!name) continue;
        auto value=osp::nodes::field<std::uint32_t>(r,*ptr,layout9::global_value);
        if (*name=="GameDaysPassed" && value) o.game_days_bits=*value;
        else if (*name=="TimeScale" && value) o.timescale_bits=*value;
    }
}
Json camera_record(Reader& reader) {
    NodeCalls calls{reader};
    auto camera=osp::scene::camera(reader,calls,state->image);
    Json::Object out;
    for (auto& field:std::get<Json::Object>(camera.data)) if (field.first!="root_tree") out.push_back(field);
    return Json::object(std::move(out));
}
osp::seq::Observation observe(Reader& r) {
    osp::seq::Observation o;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    o.clock=double(now.QuadPart-state->node_clock_origin.QuadPart)/double(state->node_clock_frequency.QuadPart);
    game_time(r,o);
    auto player=read<Address>(r,state->image+osp::layout::player_rva);
    if (player && *player) {
        auto pos=osp::nodes::field<std::array<std::uint32_t,3>>(r,*player,layout9::reference_position);
        if (pos) o.position=*pos;
        auto cell=osp::nodes::field<Address>(r,*player,layout9::reference_parent_cell);
        auto form=cell&&*cell?read<osp::layout::Form>(r,*cell):std::nullopt;
        if (form) o.parent_cell=form->id;
    }
    o.bench_index=sequencer->bench_index;
    return o;
}
Json roots_record(Reader& reader,const osp::seq::Part& part) {
    NodeCalls calls{reader}; Json::Array out;
    for (auto local:part.roots) {
        Json::Array errors; Json world;
        std::uint32_t id=sequencer->bench_index?(std::uint32_t(*sequencer->bench_index)<<24)|local:local;
        std::uint64_t reference=0;
        if (!sequencer->bench_index) errors.emplace_back("bench load index unknown");
        else if (!osp_call_word(state->image+osp::nodes::lookup_form_rva,id,0,&reference)) errors.emplace_back("LookupFormByID failed");
        auto form=reference?read<osp::layout::Form>(reader,reference):std::nullopt;
        if (reference && (!form || form->id!=id || (form->type!=64 && form->type!=65))) errors.emplace_back("not a readable REFR/ACHR");
        else if (form) {
            auto root=calls.get(reference,osp::nodes::object_root_slot,0);
            auto node=root&&*root?read<osp::nodes::Object>(reader,*root):std::nullopt;
            if (node) world=osp::nodes::transform(node->world); else errors.emplace_back("object root unavailable");
        }
        out.push_back(Json::object({{"form_id",osp::hex(id)},{"complete",errors.empty()},{"world",world},{"errors",errors}}));
    }
    return out;
}
// New screenshot files in the game folder since the previous scan, oldest first.
// Files left for a later mark (empty, or still open for writing) are recorded
// in `deferred`, so a run shows whether the plugin ever met an unfinished file.
std::vector<osp::seq::ShotFile> new_screenshots(bool baseline,Json::Array* deferred=nullptr) {
    std::vector<std::tuple<std::uint64_t,std::string,osp::seq::ShotFile>> found;
    if (sequencer->run->plan.screenshot_base.empty()) return {};
    auto pattern=sequencer->game_dir+L"\\"+wide_of(sequencer->run->plan.screenshot_base)+L"*";
    WIN32_FIND_DATAW data{};
    HANDLE find=FindFirstFileW(pattern.c_str(),&data);
    if (find==INVALID_HANDLE_VALUE) return {};
    std::size_t count=0;
    do {
        if (++count>10000) break;
        if (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        auto name=ascii_of(data.cFileName);
        if (!osp::seq::safe_component(name)) continue;
        ShotKey key{(std::uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow,
                    (std::uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime};
        auto old=sequencer->seen.find(name);
        if (old!=sequencer->seen.end() && old->second.size==key.size && old->second.time==key.time) continue;
        if (baseline) { sequencer->seen[name]=key; continue; }
        auto defer=[&](const char* why){ if (deferred) deferred->push_back(Json::object({{"file",name},{"size",key.size},{"reason",why}})); };
        if (!key.size) { defer("empty: created but not yet written"); continue; }
        // Deny writers: a file the game is still writing is left for the next mark.
        auto h=hash_file(sequencer->game_dir+L"\\"+data.cFileName,256ull*1024*1024,FILE_SHARE_READ);
        if (!h) { defer("could not be opened with writers denied"); continue; }
        sequencer->seen[name]=key;
        found.emplace_back(key.time,name,osp::seq::ShotFile{name,h->first,h->second});
    } while (FindNextFileW(find,&data));
    FindClose(find);
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){ return std::tie(std::get<0>(a),std::get<1>(a))<std::tie(std::get<0>(b),std::get<1>(b)); });
    std::vector<osp::seq::ShotFile> out; for (auto& f:found) out.push_back(std::get<2>(f));
    return out;
}
// Effective INI values (GameSettings.cpp:101-108 order: INI, then prefs).
Json ini_values(Reader& r,const std::vector<std::string>& keys,std::string& error) {
    Json::Array out;
    std::vector<std::pair<const char*,std::vector<std::pair<std::string,osp::layout::Setting>>>> lists;
    for (auto [label,rva]:{std::pair<const char*,Address>{"ini",layout9::ini_settings_rva},{"prefs",layout9::ini_pref_settings_rva}}) {
        std::vector<std::pair<std::string,osp::layout::Setting>> rows;
        auto collection=read<Address>(r,state->image+rva);
        auto node=collection&&*collection?osp::nodes::field<Address>(r,*collection,layout9::setting_list_head):std::nullopt;
        if (!node) { error+=std::string(label)+" list unreadable; "; lists.push_back({label,rows}); continue; }
        std::set<Address> visited; Address at=*node;
        while (at && visited.size()<200000 && visited.insert(at).second) {
            auto n=read<layout9::SettingNode>(r,at);
            if (!n) { error+=std::string(label)+" node unreadable; "; break; }
            auto setting=n->data?read<osp::layout::Setting>(r,n->data):std::nullopt;
            auto name=setting&&setting->name?osp::read_string(r,setting->name,256):std::nullopt;
            if (setting && name) rows.push_back({*name,*setting});
            at=n->next;
        }
        lists.push_back({label,rows});
    }
    auto lowered=[](std::string x){ return osp::seq::lower(std::move(x)); };
    auto bare=[&](const std::string& x){ return lowered(x.substr(0,x.find(':'))); };
    for (const auto& key:keys) {
        // A missing key is recorded with its reason and, as evidence for a later
        // packet, the settings whose names differ only in case or in section.
        // Candidates never count as the key: only an exact name is found.
        Json::Array candidates;
        for (const auto& [label,rows]:lists)
            for (const auto& [name,setting]:rows)
                if (candidates.size()<8 && name!=key && (lowered(name)==lowered(key) || bare(name)==bare(key))) {
                    std::optional<std::string> text;
                    if (!name.empty() && (name[0]=='s' || name[0]=='S')) text=osp::read_string(r,setting.bits,4096);
                    auto decoded=osp::decode_setting(name,setting.bits,text);
                    Json::Object o; o.push_back({"name",name}); o.push_back({"collection",label});
                    for (auto& f:std::get<Json::Object>(decoded.data)) if (f.first!="name") o.push_back(f);
                    candidates.push_back(Json::object(std::move(o)));
                }
        Json row=Json::object({{"key",key},{"found",false},{"collection",nullptr},{"type",nullptr},{"value",nullptr},{"raw_bits_hex",nullptr},
            {"reason",error.empty()?"no setting with exactly this name in the INI or prefs setting list at start":"no setting with exactly this name in the readable lists; "+error},
            {"candidates",candidates}});
        for (const auto& [label,rows]:lists) {
            auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& x){ return x.first==key; });
            if (it==rows.end()) continue;
            std::optional<std::string> text;
            if (!key.empty() && (key[0]=='s' || key[0]=='S')) text=osp::read_string(r,it->second.bits,4096);
            auto decoded=osp::decode_setting(key,it->second.bits,text);
            Json::Object o;
            o.push_back({"key",key}); o.push_back({"found",true}); o.push_back({"collection",label});
            for (auto& f:std::get<Json::Object>(decoded.data)) if (f.first!="name") o.push_back(f);
            row=Json::object(std::move(o));
            break;
        }
        out.push_back(std::move(row));
    }
    return out;
}
Json system_record(std::string& error) {
    Json windows, wine;
    struct VersionInfo { ULONG size,major,minor,build,platform; WCHAR csd[128]; } info{};
    info.size=sizeof(info);
    auto ntdll=GetModuleHandleW(L"ntdll.dll");
    using RtlGetVersion=LONG(WINAPI*)(VersionInfo*);
    using WineVersion=const char*(CDECL*)();
    auto get=ntdll?reinterpret_cast<RtlGetVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll,"RtlGetVersion"))):nullptr;
    if (get && get(&info)==0) windows=std::to_string(info.major)+"."+std::to_string(info.minor)+"."+std::to_string(info.build);
    else error+="RtlGetVersion unavailable; ";
    auto wine_get=ntdll?reinterpret_cast<WineVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll,"wine_get_version"))):nullptr;
    if (wine_get) { auto v=wine_get(); Reader r; auto text=v?osp::read_string(r,reinterpret_cast<Address>(v),64):std::nullopt; if (text) wine=*text; }
    return Json::object({{"windows_version",windows},{"wine_version",wine},
        {"wine_detected",wine_get!=nullptr}});
}
Json gpu_record(std::string& error) {
    for (DWORD i=0;i<16;++i) {
        DISPLAY_DEVICEW device{}; device.cb=sizeof(device);
        if (!EnumDisplayDevicesW(nullptr,i,&device,0)) break;
        if (device.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE) {
            std::wstring name(device.DeviceString);
            try { return Json::object({{"adapter",utf8(name.c_str())}}); } catch (...) {}
        }
    }
    error+="no primary display adapter reported; ";
    return Json();
}
void sequencer_start(Reader& reader) {
    auto& s=*sequencer;
    s.started=utc_now();
    std::string plan_error; std::optional<osp::seq::Plan> plan;
    auto plan_text=read_small(s.own_dir+L"\\packet\\plan.txt",osp::seq::max_plan_bytes);
    if (plan_text) { s.plan_sha256=osp::Sha256::of(*plan_text); plan=osp::seq::parse_plan(*plan_text,plan_error); }
    else plan_error="plan.txt unreadable or larger than 64 KiB";
    osp::seq::Plan fallback; fallback.id="unreadable-plan";
    s.run=std::make_unique<osp::seq::Run>(plan?*plan:fallback);
    s.run_id=(plan?plan->id:fallback.id)+"-"+state->session.substr(0,12);
    std::vector<osp::seq::Check> checks;
    Json::Object env; Json::Object errors;
    env.push_back({"game_version",osp::packed_version(state->runtime)});
    env.push_back({"f4se_version","0.6.23"}); env.push_back({"f4se_release_index",state->release});
    env.push_back({"plugin_version",9});
    auto dll_path=module_path(own_module()); auto exe_path=module_path(nullptr);
    auto seconds=[](){ LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return double(t.QuadPart)/double(state->node_clock_frequency.QuadPart); };
    const double t0=seconds();
    auto dll=dll_path?hash_file(*dll_path,64ull*1024*1024):std::nullopt;
    const double t1=seconds();
    auto exe=exe_path?hash_file(*exe_path,1024ull*1024*1024):std::nullopt;
    const double t2=seconds();
    auto loader=hash_file(s.game_dir+L"\\f4se_1_10_163.dll",64ull*1024*1024);
    env.push_back({"plugin_dll",file_json("osp_measure.dll",dll)}); if (!dll) errors.push_back({"plugin_dll","own DLL file unreadable"});
    env.push_back({"executable",file_json("Fallout4.exe",exe)}); if (!exe) errors.push_back({"executable","running executable file unreadable"});
    env.push_back({"f4se_runtime_dll",file_json("f4se_1_10_163.dll",loader)}); if (!loader) errors.push_back({"f4se_runtime_dll","f4se_1_10_163.dll not found in the game folder"});
    if (!plan) {
        checks.push_back({"plan",false,plan_error});
    } else {
        osp::seq::Facts facts;
        auto packet=read_small(s.own_dir+L"\\packet\\packet.json",1024*1024);
        if (packet) facts.packet_sha256=osp::Sha256::of(*packet);
        auto bench=hash_file(s.game_dir+L"\\Data\\"+wide_of(plan->bench_file),64ull*1024*1024);
        if (bench) facts.bench={true,bench->first,bench->second,""};
        if (exe) facts.executable={true,exe->first,exe->second,""};
        env.push_back({"bench_plugin",file_json(plan->bench_file,bench)}); if (!bench) errors.push_back({"bench_plugin","bench plugin not found in Data"});
        auto loaded=loaded_plugins(reader);
        if (loaded) {
            std::vector<std::pair<std::string,std::string>> names; Json::Array rows;
            for (auto& [kind,name,index]:*loaded) {
                names.emplace_back(kind,name);
                rows.push_back(Json::object({{"name",name},{"kind",kind},{"mod_index",index}}));
                if (kind=="full" && osp::seq::lower(name)==osp::seq::lower(plan->bench_file)) s.bench_index=std::uint8_t(index);
            }
            facts.plugins=names; env.push_back({"active_plugins",rows});
        } else { env.push_back({"active_plugins",nullptr}); errors.push_back({"active_plugins","loaded plugin arrays unreadable"}); }
        for (const auto& part:plan->parts) {
            auto bytes=read_small(s.game_dir+L"\\"+wide_of(part.name)+L".txt",osp::seq::max_bat_bytes+1);
            facts.parts.push_back(bytes?osp::seq::FileFact{true,bytes->size(),osp::Sha256::of(*bytes),*bytes}:osp::seq::FileFact{});
        }
        auto o=observe(reader);
        facts.in_world=o.parent_cell.has_value();
        checks=osp::seq::preflight(*plan,facts);
        std::string ini_error;
        env.push_back({"ini",ini_values(reader,plan->ini_keys,ini_error)});
        if (!ini_error.empty()) errors.push_back({"ini",ini_error});
        if (!o.game_days_bits || !o.timescale_bits) checks.push_back({"game_time",true,"GameDaysPassed/TimeScale not found: simulated seconds will be null"});
    }
    std::string system_error,gpu_error;
    env.push_back({"system",system_record(system_error)}); if (!system_error.empty()) errors.push_back({"system",system_error});
    auto gpu=gpu_record(gpu_error); env.push_back({"gpu",gpu}); if (!gpu_error.empty()) errors.push_back({"gpu",gpu_error});
    auto window=window_snapshot(); auto rect=osp::scene::member(window,"client_rect");
    auto values=rect?std::get_if<Json::Array>(&rect->data):nullptr;
    if (values && values->size()==4) {
        auto w=std::get_if<std::int64_t>(&(*values)[2].data), h=std::get_if<std::int64_t>(&(*values)[3].data);
        env.push_back({"client_size",w&&h?Json(Json::Array{Json(*w),Json(*h)}):Json()});
    } else { env.push_back({"client_size",nullptr}); errors.push_back({"client_size","the game window was not the foreground window"}); }
    env.push_back({"start_timings",Json::object({{"plugin_dll_hash_seconds",t1-t0},{"executable_hash_seconds",t2-t1},{"start_seconds",seconds()-t0}})});
    env.push_back({"errors",Json::object(std::move(errors))});
    s.environment=Json::object(std::move(env));
    s.preflight=osp::seq::checks_json(checks);
    auto o=observe(reader);
    auto d=s.run->start(checks,o);
    new_screenshots(true);
    for (const auto& line:d.console) console_print(line);
    s.marks.push_back(osp::seq::mark_json(0,d,o,utc_now(),Json(),Json::Array{},d.console));
    if (!write_run_json()) console_print("osp: ERROR cannot write run.json in osp_measure\\runs");
}
bool sequencer_mark(double* result) noexcept {
    if (result) { double zero=0; SIZE_T done=0; WriteProcessMemory(GetCurrentProcess(),result,&zero,sizeof(zero),&done); }
    if (InterlockedCompareExchange(&state->busy,1,0)!=0) return true;
    struct Reset { ~Reset() { InterlockedExchange(&state->busy,0); } } reset;
    try {
        Reader reader; auto& s=*sequencer;
        if (!s.run) { sequencer_start(reader); return true; }
        if (InterlockedExchange(&s.pending_abort,0)) {
            s.run->abort(s.pending_reason);
            console_print("osp: RUN ABORTED: "+s.pending_reason);
            write_run_json();
        }
        auto o=observe(reader);
        auto part=s.run->expected();
        Json camera;
        if (s.run->wants_camera()) { camera=camera_record(reader); osp::seq::apply_camera(camera,o); }
        auto d=s.run->mark(o);
        if (d.outcome==osp::seq::Outcome::ignored) { for (const auto& line:d.console) console_print(line); return true; }
        if (part && part->role==osp::seq::Role::cell && d.outcome==osp::seq::Outcome::advanced) s.cell_confirmed=true;
        Json roots=d.read_roots&&part?roots_record(reader,*part):Json(Json::Array{});
        Json::Array deferred;
        s.run->bind(new_screenshots(false,&deferred));
        for (const auto& line:d.console) console_print(line);
        s.marks.push_back(osp::seq::mark_json(s.marks.size(),d,o,utc_now(),camera,roots,d.console,
                                              Json::object({{"deferred",deferred}})));
        if (!write_run_json()) console_print("osp: ERROR cannot write run.json in osp_measure\\runs");
        if (result) { double one=1; SIZE_T done=0; WriteProcessMemory(GetCurrentProcess(),result,&one,sizeof(one),&done); }
    } catch (...) { console_print("osp: ERROR sequencer mark failed; see run.json"); }
    return true;
}
// Every F4SE message is journaled while a packet is installed (round 25 could
// not say which messages a coc from the main menu produces).
void journal_message(std::uint32_t type,std::uint32_t length) noexcept {
    try {
        if (!sequencer) return;
        if (sequencer->f4se_messages.size()>=max_f4se_messages) { InterlockedIncrement(&sequencer->messages_unrecorded); return; }
        if (InterlockedCompareExchange(&state->busy,1,0)!=0) { InterlockedIncrement(&sequencer->messages_unrecorded); return; }
        struct Reset { ~Reset() { InterlockedExchange(&state->busy,0); } } reset;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        auto& s=*sequencer;
        s.f4se_messages.push_back(Json::object({{"type",type},{"name",osp::seq::f4se_message_name(type)},{"data_length",length},
            {"utc",utc_now()},{"qpc_seconds",double(now.QuadPart-state->node_clock_origin.QuadPart)/double(state->node_clock_frequency.QuadPart)},
            {"run_status",s.run?osp::seq::status_name(s.run->status):"not started"},{"after_mark",s.marks.size()}}));
        if (s.run) write_run_json();
    } catch (...) {}
}
// Saves and loads during a run void it; nothing here can prevent them.
void sequencer_message(std::uint32_t type) noexcept {
    try {
        if (!sequencer || !sequencer->run || sequencer->run->status!=osp::seq::Status::in_progress) return;
        std::string why;
        if (type==F4SEMessagingInterface::kMessage_PreSaveGame) why="the game started a save during the run";
        else if (sequencer->cell_confirmed && (type==F4SEMessagingInterface::kMessage_PreLoadGame ||
                 type==F4SEMessagingInterface::kMessage_PostLoadGame || type==F4SEMessagingInterface::kMessage_NewGame))
            why="a saved game was loaded or a new game started during the run";
        if (why.empty()) return;
        if (InterlockedCompareExchange(&state->busy,1,0)!=0) {
            sequencer->pending_reason=why; InterlockedExchange(&sequencer->pending_abort,1); return;
        }
        struct Reset { ~Reset() { InterlockedExchange(&state->busy,0); } } reset;
        sequencer->run->abort(why);
        write_run_json();
    } catch (...) {}
}
void on_message(F4SEMessagingInterface::Message* incoming) noexcept {
    if (!state) return;
    try {
        Reader r;
        auto msg=read<F4SEMessagingInterface::Message>(r,reinterpret_cast<Address>(incoming));
        if (!msg) { log_line("ERROR null or unreadable F4SE message"); return; }
        auto sender=osp::read_string(r,reinterpret_cast<Address>(msg->sender),64);
        if (!sender || *sender!="F4SE") { log_line("ERROR invalid F4SE message sender"); return; }
        const bool armed=sequencer_armed();
        if (armed) journal_message(msg->type,msg->dataLen);
        if (msg->type==F4SEMessagingInterface::kMessage_PostLoad) { register_console(); return; }
        // v9: while a packet is installed no legacy snapshot is written; saves and
        // loads during a run are recorded as its abort reason.
        if (armed) { sequencer_message(msg->type); return; }
        auto why=osp::trigger(msg->type,reinterpret_cast<Address>(msg->data),msg->dataLen);
        if (why==osp::Trigger::none) {
            if (msg->type==F4SEMessagingInterface::kMessage_PostLoadGame)
                log_line("load failed or unexpected success payload; no snapshot");
            return;
        }
        if (InterlockedCompareExchange(&state->busy,1,0)!=0) {
            log_line("ERROR reentrant snapshot skipped"); return;
        }
        struct BusyReset { ~BusyReset() { InterlockedExchange(&state->busy,0); } } reset;
        capture(why);
    } catch (const std::exception& e) { log_line(std::string("ERROR snapshot failed: ")+e.what()); }
      catch (...) { log_line("ERROR snapshot failed: unknown C++ exception"); }
}
}

extern "C" __declspec(dllexport)
bool F4SEPlugin_Query(const F4SEInterface* f4se,PluginInfo* info) noexcept {
    if (!info) return false;
    info->infoVersion=PluginInfo::kInfoVersion;
    info->name="osp_measure"; info->version=9;
    if (!f4se) return false;
    // Query has no I/O, registration, game access, or release-index callback.
    return !f4se->isEditor && f4se->runtimeVersion==RUNTIME_VERSION_1_10_163 &&
           f4se->f4seVersion==PACKED_F4SE_VERSION;
}
extern "C" __declspec(dllexport)
bool F4SEPlugin_Load(const F4SEInterface* f4se) noexcept {
    if (state) return false;
    try {
        auto s=std::make_unique<State>();
        wchar_t documents[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents))) {
            log_line("ERROR cannot resolve Documents folder"); return false;
        }
        s->directory=std::wstring(documents)+L"\\My Games\\Fallout4\\F4SE";
        auto result=SHCreateDirectoryExW(nullptr,s->directory.c_str(),nullptr);
        if (result!=ERROR_SUCCESS && result!=ERROR_ALREADY_EXISTS && result!=ERROR_FILE_EXISTS) {
            log_line("ERROR cannot create F4SE log directory"); return false;
        }
        s->log_path=s->directory+L"\\osp_measure.log";
        state=s.release();
        if(!QueryPerformanceFrequency(&state->node_clock_frequency) || state->node_clock_frequency.QuadPart<=0 ||
           !QueryPerformanceCounter(&state->node_clock_origin)) {
            log_line("ERROR monotonic node clock unavailable");return false;
        }
        state->settings_path=settings_file_path();
        result=SHCreateDirectoryExW(nullptr,(state->directory+L"\\osp-measure").c_str(),nullptr);
        if (result!=ERROR_SUCCESS && result!=ERROR_ALREADY_EXISTS && result!=ERROR_FILE_EXISTS) {
            log_line("ERROR cannot create measurement directory"); return false;
        }
        if (!f4se || !f4se->GetReleaseIndex || !f4se->QueryInterface || !f4se->GetPluginHandle) {
            log_line("ERROR missing F4SE interface"); return false;
        }
        state->runtime=f4se->runtimeVersion; state->f4se=f4se->f4seVersion;
        state->release=f4se->GetReleaseIndex();
        if (!osp::compatible(state->runtime,state->f4se,f4se->isEditor,state->release)) {
            log_line("ERROR unsupported runtime/F4SE/release index"); return false;
        }
        state->image=reinterpret_cast<Address>(GetModuleHandleW(nullptr));
        if (!state->image) { log_line("ERROR missing game image base"); return false; }
        unsigned char random[16]{};
        if (BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) {
            log_line("ERROR cannot create session ID"); return false;
        }
        for (auto b:random) state->session+=osp::hex(b,2);
        state->handle=f4se->GetPluginHandle();
        auto messaging=static_cast<F4SEMessagingInterface*>(f4se->QueryInterface(kInterface_Messaging));
        if (state->handle==kPluginHandle_Invalid || !messaging ||
            messaging->interfaceVersion<F4SEMessagingInterface::kInterfaceVersion || !messaging->RegisterListener) {
            log_line("ERROR messaging interface unavailable"); return false;
        }
        log_line("loading osp_measure 9; runtime="+osp::hex(state->runtime)+
                 "; F4SE=0.6.23; session="+state->session);
        log_line("GameSettings coverage: names from "+utf8(state->settings_path.c_str())+"; not all engine settings");
        if (!messaging->RegisterListener(state->handle,"F4SE",on_message)) {
            log_line("ERROR RegisterListener failed"); return false;
        }
        return true;
    } catch (const std::exception& e) { log_line(std::string("ERROR Load failed: ")+e.what()); return false; }
      catch (...) { log_line("ERROR Load failed: unknown C++ exception"); return false; }
}
