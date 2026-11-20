// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
// Plugin v9 stepped sequencer (route B, docs/sequencer.md). Pure logic: no
// Windows API, no game memory. The DLL gathers observations and files; this
// header decides, and the native tests exercise every decision.
#include "osp/json.hpp"
#include "osp/sha256.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace osp::seq {
constexpr int plan_format=2;
constexpr std::size_t max_parts=256, max_bat_bytes=4096, max_bat_lines=16, max_plan_bytes=65536;
constexpr char mark_command[]="ospsnap";

// ------------------------------------------------------------------ helpers
inline bool is_hex(const std::string& s,std::size_t n,bool upper) {
    if (s.size()!=n) return false;
    for (char c:s) if (!((c>='0'&&c<='9')||(upper?(c>='A'&&c<='F'):(c>='a'&&c<='f')))) return false;
    return true;
}
inline std::uint32_t parse_hex(const std::string& s) {
    std::uint32_t v=0;
    for (char c:s) v=(v<<4)|std::uint32_t(c<='9'?c-'0':c>='a'?c-'a'+10:c-'A'+10);
    return v;
}
inline std::optional<std::uint64_t> parse_uint(const std::string& s,std::uint64_t limit) {
    if (s.empty() || s.size()>12 || (s.size()>1 && s[0]=='0')) return std::nullopt;
    std::uint64_t v=0;
    for (char c:s) { if (c<'0'||c>'9') return std::nullopt; v=v*10+std::uint64_t(c-'0'); }
    if (v>limit) return std::nullopt;
    return v;
}
inline std::string lower(std::string s) { for (auto& c:s) if (c>='A'&&c<='Z') c=char(c-'A'+'a'); return s; }
inline std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out; std::size_t b=0;
    while (true) { auto e=s.find(' ',b); out.push_back(s.substr(b,e==std::string::npos?e:e-b)); if (e==std::string::npos) break; b=e+1; }
    return out;
}
inline double word_value(std::uint32_t bits) {
    if ((bits&0x7f800000u)==0x7f800000u) return std::nan("");
    float f; std::memcpy(&f,&bits,4); return double(f);
}
// A file or folder name the sequencer may create or read: one component only.
inline bool safe_component(const std::string& s) {
    if (s.empty() || s.size()>96 || s=="." || s=="..") return false;
    for (unsigned char c:s)
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
    return s.front()!='.' && s.back()!='.';
}
// Join a base folder with checked components; anything else is refused, so a
// write can never leave the plugin's own folder.
inline std::optional<std::wstring> child(const std::wstring& base,const std::vector<std::string>& parts) {
    if (base.empty() || parts.empty()) return std::nullopt;
    std::wstring out=base;
    for (const auto& p:parts) { if (!safe_component(p)) return std::nullopt; out+=L'\\'; out.append(p.begin(),p.end()); }
    return out;
}
// Console text: printable ASCII, no '%' (the engine print call is not
// documented as format-free), bounded length.
inline std::string console_text(const std::string& s) {
    std::string out;
    for (unsigned char c:s) { if (out.size()>=240) break; out.push_back(c>=32&&c<127&&c!='%'?char(c):'?'); }
    return out;
}
// One console line: the print call does not end a line itself (round 25 showed
// consecutive messages and the next command's echo running together), so every
// message carries its own terminating newline.
inline std::string console_line(const std::string& s) { return console_text(s)+"\n"; }

// ---------------------------------------------------------- closed list
struct CommandCheck { bool ok=false; std::string verb,why; };
inline bool decimal_text(const std::string& s) {
    std::size_t i=s[0]=='-'?1:0, digits=0, fraction=0; bool dot=false;
    if (i>=s.size()) return false;
    for (;i<s.size();++i) {
        char c=s[i];
        if (c=='.') { if (dot || !digits) return false; dot=true; continue; }
        if (c<'0'||c>'9') return false;
        (dot?fraction:digits)++;
    }
    return digits>=1 && digits<=7 && (!dot || (fraction>=1 && fraction<=9));
}
inline CommandCheck check_command(const std::string& line,const std::string& cell) {
    CommandCheck r;
    if (line.empty() || line.size()>96) { r.why="empty or longer than 96 characters"; return r; }
    for (unsigned char c:line) if (c<32 || c>=127 || c==';' || c=='"') { r.why="non-printable, ; or quote"; return r; }
    auto parts=split(line);
    for (const auto& p:parts) if (p.empty()) { r.why="single spaces only"; return r; }
    r.verb=lower(parts[0]);
    const auto n=parts.size();
    auto axis=[&](const std::string& a){return a=="x"||a=="y"||a=="z";};
    if (r.verb=="player.setpos") {
        if (n!=3 || !axis(parts[1]) || !decimal_text(parts[2])) { r.why="player.setpos takes one axis and one decimal"; return r; }
    } else if (r.verb=="player.setangle") {
        const auto& v=parts.size()==3?parts[2]:std::string();
        std::size_t i=!v.empty()&&v[0]=='-'?1:0;
        bool digits=n==3 && v.size()>i && v.size()-i<=3;
        for (std::size_t k=i;digits&&k<v.size();++k) digits=v[k]>='0'&&v[k]<='9';
        if (!digits || !axis(parts[1]) || std::stoi(v)<-360 || std::stoi(v)>360) { r.why="player.setangle takes one axis and an integer in [-360, 360]"; return r; }
    } else if (r.verb=="tfc") {
        if (!(n==1 || (n==2 && parts[1]=="1"))) { r.why="tfc takes no argument or 1"; return r; }
    } else if (r.verb=="coc") {
        if (n!=2 || cell.empty() || parts[1]!=cell) { r.why="coc is allowed only with the packet bench cell"; return r; }
    } else if (r.verb=="tm" || r.verb=="tcl" || r.verb=="screenshot" || r.verb==mark_command) {
        if (n!=1) { r.why=parts[0]+" takes no argument"; return r; }
    } else { r.why="command outside the closed list: "+parts[0]; return r; }
    r.ok=true; return r;
}
struct BatCheck { bool ok=false; std::string why; unsigned shots=0; std::vector<std::string> lines; };
inline BatCheck check_bat(const std::string& bytes,const std::string& cell) {
    BatCheck r;
    if (bytes.size()>max_bat_bytes) { r.why="part file larger than 4096 bytes"; return r; }
    std::string text=bytes;
    if (!text.empty() && text.back()=='\n') text.pop_back();
    std::size_t b=0;
    while (true) {
        auto e=text.find('\n',b);
        auto line=text.substr(b,e==std::string::npos?e:e-b);
        if (!line.empty() && line.back()=='\r') line.pop_back();
        r.lines.push_back(line);
        if (e==std::string::npos) break;
        b=e+1;
    }
    if (bytes.empty() || r.lines.size()>max_bat_lines) { r.why="part file must hold 1 to 16 lines"; return r; }
    unsigned marks=0, others=0;
    for (const auto& line:r.lines) {
        auto c=check_command(line,cell);
        if (!c.ok) { r.why=c.why; return r; }
        marks+=c.verb==mark_command; r.shots+=c.verb=="screenshot";
        others+=c.verb!=mark_command && c.verb!="screenshot";
    }
    if (marks!=1) { r.why="part file must contain exactly one ospsnap"; return r; }
    // The game takes the picture after the whole file has run (round 25), so any
    // interface, camera or pose command in the same file would act on it.
    if (r.shots>1) { r.why="a part file may request at most one Screenshot"; return r; }
    if (r.shots && others) { r.why="a part file with Screenshot may hold only Screenshot and ospsnap"; return r; }
    r.ok=true; return r;
}
// Interface (tm) and free camera (tfc) state across all part files in typed
// order: every Screenshot must find the interface hidden and the free camera
// on, and the last part must leave both as they were at the start.
struct LayoutCheck { bool ok=false; std::string why; };
inline LayoutCheck check_layout(const std::vector<std::pair<std::string,std::vector<std::string>>>& parts) {
    LayoutCheck r; bool hidden=false, free=false;
    for (const auto& [name,lines]:parts)
        for (const auto& line:lines) {
            auto verb=lower(split(line)[0]);
            if (verb=="tm") hidden=!hidden;
            else if (verb=="tfc") free=!free;
            else if (verb=="screenshot" && !(hidden && free)) {
                r.why="part "+name+": Screenshot while the interface is shown or the free camera is off"; return r;
            }
        }
    if (hidden || free) { r.why="the parts leave the interface hidden or the free camera on"; return r; }
    r.ok=true; return r;
}

// ------------------------------------------------------------------ plan
// photo_v2 roles (docs/packet.md). capture hides the interface; photo and
// repeat each hold one Screenshot and nothing else; restore shows it again.
enum class Role { start, note, cell, pose, settle, free, capture, photo, repeat, restore, end };
constexpr const char* role_names[]={"start","note","cell","pose","settle","free","capture","photo","repeat","restore","end"};
inline std::optional<Role> role_of(const std::string& s) {
    for (int i=0;i<11;++i) if (s==role_names[i]) return Role(i);
    return std::nullopt;
}
inline const char* role_name(Role r) { return role_names[int(r)]; }
constexpr std::size_t step_parts=7;
struct Part {
    std::string name,sha256,step; Role role=Role::note; unsigned shots=0;
    unsigned settle_seconds=0; bool simulated_required=false;
    std::array<std::uint32_t,3> position{}; std::uint32_t tolerance=0;
    unsigned free_seconds=0; std::vector<std::uint32_t> roots;
};
struct Plan {
    std::string id,packet_sha256,bench_file,bench_sha256,cell,screenshot_base,exe_sha256; unsigned version=0;
    std::uint64_t bench_size=0,exe_size=0; std::uint32_t cell_local=0;
    std::vector<std::pair<std::string,std::string>> plugins; // kind, name
    std::vector<std::string> ini_keys;
    std::vector<Part> parts;
    std::size_t steps() const { std::size_t n=0; for (const auto& p:parts) n+=p.role==Role::pose; return n; }
};
inline std::string unescape(std::string s) { for (auto& c:s) if (c=='|') c=' '; return s; }
inline bool plan_name(const std::string& s) {
    if (s.empty() || s.size()>95) return false;
    for (unsigned char c:s) if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='|'||c==':')) return false;
    return s.find("..")==std::string::npos;
}
// A setting name as a packet requests it (mirrors the ini_keys rule in
// tools/packet_format.py): a letter, then letters, digits, spaces or '_', 64
// characters at most, and an optional ":Section". Round 26 found the autosave
// settings in the prefs list under names without a section (bSaveOnTravel).
inline bool ini_name(const std::string& s) {
    auto word=[](unsigned char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'; };
    auto colon=s.find(':');
    std::string name=s.substr(0,colon);
    if (name.empty() || name.size()>64 || !((name[0]>='a'&&name[0]<='z')||(name[0]>='A'&&name[0]<='Z'))) return false;
    for (unsigned char c:name) if (!word(c) && c!=' ') return false;
    if (colon==std::string::npos) return true;
    std::string section=s.substr(colon+1);
    if (section.empty() || section.size()>32) return false;
    for (unsigned char c:section) if (!word(c)) return false;
    return true;
}
inline std::optional<Plan> parse_plan(const std::string& text,std::string& error) {
    Plan plan; error.clear();
    auto fail=[&](const std::string& why,std::size_t line){ error="plan line "+std::to_string(line)+": "+why; return std::optional<Plan>(); };
    if (text.empty() || text.size()>max_plan_bytes || text.back()!='\n') return fail("empty, too large or unterminated",0);
    std::vector<std::string> lines; std::size_t b=0;
    while (b<text.size()) { auto e=text.find('\n',b); lines.push_back(text.substr(b,e-b)); b=e+1; }
    for (const auto& l:lines) for (unsigned char c:l) if (c<32||c>=127) return fail("non-ASCII byte",0);
    std::size_t i=0; bool ended=false;
    auto next=[&]()->std::vector<std::string>{ return i<lines.size()?split(lines[i++]):std::vector<std::string>{}; };
    auto t=next();
    if (t!=std::vector<std::string>{"osp-plan",std::to_string(plan_format)}) return fail("header must be osp-plan "+std::to_string(plan_format),1);
    t=next();
    if (t.size()!=4 || t[0]!="packet" || !plan_name(t[1]) || !is_hex(t[3],64,false)) return fail("bad packet line",i);
    auto version=parse_uint(t[2],1000000);
    if (!version || !*version) return fail("bad packet version",i);
    plan.id=t[1]; plan.version=unsigned(*version); plan.packet_sha256=t[3];
    t=next();
    if (t.size()!=4 || t[0]!="bench" || !plan_name(t[1]) || !is_hex(t[3],64,false)) return fail("bad bench line",i);
    auto size=parse_uint(t[2],64ull*1024*1024);
    if (!size || !*size) return fail("bad bench size",i);
    plan.bench_file=unescape(t[1]); plan.bench_size=*size; plan.bench_sha256=t[3];
    if (lower(plan.bench_file).size()<5 || lower(plan.bench_file).substr(plan.bench_file.size()-4)!=".esp" || plan.bench_file.find(':')!=std::string::npos)
        return fail("bench must be a plain .esp name",i);
    t=next();
    if (t.size()!=3 || t[0]!="cell" || t[1].empty() || t[1].size()>64 || !is_hex(t[2],6,true)) return fail("bad cell line",i);
    for (char c:t[1]) if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return fail("bad cell EditorID",i);
    plan.cell=t[1]; plan.cell_local=parse_hex(t[2]);
    t=next();
    if (t.size()!=2 || t[0]!="screenshot" || !safe_component(t[1])) return fail("bad screenshot line",i);
    plan.screenshot_base=t[1];
    t=next();
    if (t.size()!=3 || t[0]!="exe" || !is_hex(t[2],64,false)) return fail("bad exe line",i);
    auto exe=parse_uint(t[1],1024ull*1024*1024);
    if (!exe || !*exe) return fail("bad exe size",i);
    plan.exe_size=*exe; plan.exe_sha256=t[2];
    std::vector<std::string> step_names;
    while (i<lines.size()) {
        t=next();
        if (t.empty()) return fail("empty line",i);
        if (t[0]=="plugin") {
            if (t.size()!=3 || (t[1]!="full" && t[1]!="light") || !plan_name(t[2]) || t[2].find(':')!=std::string::npos || !plan.parts.empty())
                return fail("bad plugin line",i);
            plan.plugins.emplace_back(t[1],unescape(t[2]));
        } else if (t[0]=="ini") {
            if (t.size()!=2 || !plan_name(t[1]) || !ini_name(unescape(t[1])) || !plan.parts.empty()) return fail("bad ini line",i);
            plan.ini_keys.push_back(unescape(t[1]));
        } else if (t[0]=="part") {
            if (t.size()<6 || !safe_component(t[1]) || t[1].size()>32 || !is_hex(t[2],64,false)) return fail("bad part line",i);
            Part p; p.name=t[1]; p.sha256=t[2]; p.step=t[4];
            auto role=role_of(t[3]); auto shots=parse_uint(t[5],8);
            if (!role || !shots) return fail("bad part role or shots",i);
            p.role=*role; p.shots=unsigned(*shots);
            bool stepless=p.role==Role::start||p.role==Role::note||p.role==Role::cell||p.role==Role::end;
            if (stepless ? p.step!="-" : !safe_component(p.step)) return fail("bad part step",i);
            std::size_t extra=t.size()-6;
            if (p.role==Role::settle) {
                auto s=extra==6?parse_uint(t[6],600):std::nullopt;
                if (!s || !*s || (t[7]!="require" && t[7]!="record")) return fail("bad settle arguments",i);
                for (int k=0;k<4;++k) if (!is_hex(t[8+k],8,true)) return fail("bad settle word",i);
                p.settle_seconds=unsigned(*s); p.simulated_required=t[7]=="require";
                for (int k=0;k<3;++k) p.position[k]=parse_hex(t[8+k]);
                p.tolerance=parse_hex(t[11]);
                double tol=word_value(p.tolerance);
                if (!(tol>0 && tol<=1)) return fail("bad tolerance",i);
                for (auto w:p.position) if (std::isnan(word_value(w))) return fail("non-finite position",i);
            } else if (p.role==Role::capture) {
                auto f=extra>=2?parse_uint(t[6],60):std::nullopt; auto n=extra>=2?parse_uint(t[7],8):std::nullopt;
                if (!f || !n || extra!=2+*n) return fail("bad capture arguments",i);
                p.free_seconds=unsigned(*f);
                for (std::size_t k=0;k<*n;++k) { if (!is_hex(t[8+k],6,true)) return fail("bad root id",i); p.roots.push_back(parse_hex(t[8+k])); }
            } else if (extra) return fail("unexpected part arguments",i);
            for (const auto& q:plan.parts) if (q.name==p.name) return fail("duplicate part name",i);
            plan.parts.push_back(std::move(p));
            if (plan.parts.size()>max_parts) return fail("too many parts",i);
        } else if (t[0]=="end") {
            auto n=t.size()==2?parse_uint(t[1],max_parts):std::nullopt;
            if (!n || *n!=plan.parts.size() || i!=lines.size()) return fail("end count differs or text after end",i);
            ended=true;
        } else return fail("unknown line "+t[0],i);
    }
    if (!ended) return fail("missing end line",i);
    if (plan.plugins.empty() || lower(plan.plugins.back().second)!=lower(plan.bench_file) || plan.plugins.back().first!="full")
        return fail("the bench plugin must be the last active plugin",0);
    // Structure: start, note, cell, then pose/settle/free/capture/photo/repeat/restore per step, then end.
    const auto& ps=plan.parts;
    if (ps.size()<4+step_parts || ps[0].role!=Role::start || ps[1].role!=Role::note || ps[2].role!=Role::cell || ps.back().role!=Role::end || (ps.size()-4)%step_parts)
        return fail("parts do not follow the photo_v2 structure",0);
    for (std::size_t k=3;k+1<ps.size();k+=step_parts) {
        const Role want[step_parts]={Role::pose,Role::settle,Role::free,Role::capture,Role::photo,Role::repeat,Role::restore};
        const unsigned shots[step_parts]={0,0,0,0,1,1,0};
        for (std::size_t j=0;j<step_parts;++j)
            if (ps[k+j].role!=want[j] || ps[k+j].step!=ps[k].step || ps[k+j].shots!=shots[j]) return fail("step parts out of order: "+ps[k+j].name,0);
        for (const auto& s:step_names) if (s==ps[k].step) return fail("duplicate step "+s,0);
        step_names.push_back(ps[k].step);
    }
    for (auto k:{std::size_t(0),std::size_t(1),std::size_t(2),ps.size()-1}) if (ps[k].shots) return fail("setup parts take no screenshot",0);
    return plan;
}

// ---------------------------------------------------------------- preflight
struct Check { std::string name; bool passed=false; std::string detail; };
struct FileFact { bool found=false; std::uint64_t size=0; std::string sha256,bytes; };
struct Facts {
    std::string packet_sha256;                  // of packet.json; empty if unreadable
    FileFact bench, executable;
    std::optional<std::vector<std::pair<std::string,std::string>>> plugins; // kind, name, load order
    std::vector<FileFact> parts;                // in plan order; bytes kept for the check
    bool in_world=false;                        // player already has a parent cell
};
inline std::vector<Check> preflight(const Plan& plan,const Facts& f) {
    std::vector<Check> out;
    auto add=[&](std::string name,bool ok,std::string detail){ out.push_back({std::move(name),ok,std::move(detail)}); };
    add("packet_sha256",f.packet_sha256==plan.packet_sha256,
        f.packet_sha256.empty()?"packet.json unreadable":"packet.json "+f.packet_sha256+", plan names "+plan.packet_sha256);
    add("executable",f.executable.found && f.executable.size==plan.exe_size && f.executable.sha256==plan.exe_sha256,
        !f.executable.found?"running executable unreadable":"size "+std::to_string(f.executable.size)+", sha256 "+f.executable.sha256+
        "; the packet requires size "+std::to_string(plan.exe_size)+", sha256 "+plan.exe_sha256);
    add("bench_plugin",f.bench.found && f.bench.size==plan.bench_size && f.bench.sha256==plan.bench_sha256,
        !f.bench.found?"bench plugin not found in Data":"size "+std::to_string(f.bench.size)+", sha256 "+f.bench.sha256);
    bool plugins_ok=false; std::string detail="active plugin list unreadable";
    if (f.plugins) {
        plugins_ok=f.plugins->size()==plan.plugins.size();
        for (std::size_t k=0;plugins_ok && k<plan.plugins.size();++k)
            plugins_ok=(*f.plugins)[k].first==plan.plugins[k].first && lower((*f.plugins)[k].second)==lower(plan.plugins[k].second);
        detail=std::to_string(f.plugins->size())+" active, "+std::to_string(plan.plugins.size())+" required";
        if (!plugins_ok) for (std::size_t k=0;k<f.plugins->size() && k<plan.plugins.size();++k)
            if (lower((*f.plugins)[k].second)!=lower(plan.plugins[k].second) || (*f.plugins)[k].first!=plan.plugins[k].first) {
                detail+="; first difference at "+std::to_string(k)+": "+(*f.plugins)[k].second; break;
            }
    }
    add("active_plugins",plugins_ok,detail);
    std::vector<std::pair<std::string,std::vector<std::string>>> layout; bool readable=true;
    for (std::size_t k=0;k<plan.parts.size();++k) {
        const auto& p=plan.parts[k];
        const FileFact* file=k<f.parts.size()?&f.parts[k]:nullptr;
        if (!file || !file->found) { add("part "+p.name,false,"file "+p.name+".txt not found in the game folder"); readable=false; continue; }
        auto bat=check_bat(file->bytes,plan.cell);
        if (!bat.ok) { add("part "+p.name,false,bat.why); readable=false; continue; }
        layout.emplace_back(p.name,bat.lines);
        if (bat.shots!=p.shots) { add("part "+p.name,false,"Screenshot count differs from the plan"); continue; }
        add("part "+p.name,file->sha256==p.sha256,file->sha256==p.sha256?"matches":"sha256 "+file->sha256+" differs from the plan");
    }
    if (readable) { auto l=check_layout(layout); add("layout",l.ok,l.ok?"every Screenshot finds the interface hidden and the free camera on":l.why); }
    // Recorded, not refused: whether the main-menu player has no parent cell is
    // still to be confirmed live, and a save-game start is detected separately.
    add("start_state",true,f.in_world?"player already in a cell: started after loading a save (allowed; recorded)":"main menu: no parent cell");
    return out;
}

// ------------------------------------------------------------------ run
struct Observation {
    double clock=0;                                  // seconds since plugin load
    std::optional<std::uint32_t> game_days_bits,timescale_bits; // TESGlobal value words
    std::optional<std::array<std::uint32_t,3>> position;
    std::optional<std::uint32_t> parent_cell;
    std::optional<std::uint32_t> camera_state; bool camera_complete=false;
    std::optional<std::uint8_t> bench_index;
};
// F4SE message types, PluginAPI.h:97-115 (enum order), for the run's message journal.
inline const char* f4se_message_name(std::uint32_t type) {
    static const char* n[]={"PostLoad","PostPostLoad","PreLoadGame","PostLoadGame","PreSaveGame","PostSaveGame",
                            "DeleteGame","InputLoaded","NewGame","GameLoaded","GameDataReady"};
    return type<11?n[type]:"unknown";
}
enum class Outcome { advanced, not_settled, aborted, ignored };
inline const char* outcome_name(Outcome o) {
    static const char* n[]={"advanced","not_settled","aborted","ignored"}; return n[int(o)];
}
enum class Status { idle, refused, in_progress, complete, aborted };
inline const char* status_name(Status s) {
    static const char* n[]={"idle","refused","in_progress","complete","aborted"}; return n[int(s)];
}
struct Decision {
    Outcome outcome=Outcome::ignored; const Part* part=nullptr;
    std::vector<Check> checks; std::vector<std::string> console;
    std::optional<double> wall_since,simulated_since;
    bool read_camera=false, read_roots=false;
    std::vector<std::pair<std::string,Json>> observed;   // recorded facts, never a gate
};
struct ShotFile { std::string name; std::uint64_t size=0; std::string sha256; };
struct BoundShot { ShotFile file; std::string step,shot,status; std::size_t seen_at_mark=0; };
struct Pending { std::string step,shot; std::size_t requested_at_mark=0; };

inline bool within(const std::array<std::uint32_t,3>& got,const std::array<std::uint32_t,3>& want,std::uint32_t tol) {
    double t=word_value(tol);
    for (int k=0;k<3;++k) {
        double a=word_value(got[k]),b=word_value(want[k]);
        if (std::isnan(a) || std::isnan(b) || std::fabs(a-b)>t) return false;
    }
    return true;
}
inline std::string fixed(double v,int digits=1) {
    char s[64]; std::snprintf(s,sizeof(s),"%.*f",digits,v); return s;
}

// Settle timing (docs/sequencer.md section 4). Round 25 showed that the pose
// commands take effect only after their part file has run, so the pose mark
// never sees the new position. The clock therefore starts at the pose mark,
// as "command sent". If a later settle mark still does not see the pose, the
// command may have come from that file instead (parts typed out of order), and
// the clock restarts there once; a second miss after a full wait gives up with
// a warning, and the step cannot qualify.
struct SettleClock {
    bool started=false; double wall=0; std::optional<double> days;
    std::size_t origin_mark=0; std::string origin_role; unsigned misses=0;
};
class Run {
    std::map<std::string,SettleClock> clocks_;
    std::map<std::string,double> free_clock_;
    std::string next_line() const { auto p=expected(); return p?"osp: next: bat "+p->name:std::string(); }
public:
    explicit Run(Plan p) : plan(std::move(p)) {}
    Plan plan;
    Status status=Status::idle;
    std::vector<std::string> reasons;
    std::size_t next=0, marks=0;
    std::vector<BoundShot> shots;
    std::vector<Pending> pending;

    const Part* expected() const { return status==Status::in_progress && next<plan.parts.size()?&plan.parts[next]:nullptr; }
    // The camera is read before these marks so that mark() can judge or record it.
    bool wants_camera() const {
        auto p=expected();
        return p && (p->role==Role::settle || p->role==Role::capture || p->role==Role::photo || p->role==Role::repeat || p->role==Role::restore);
    }
    std::string type_next() const { auto p=expected(); return p?"bat "+p->name:std::string(); }

    Decision start(const std::vector<Check>& checks,const Observation&) {
        Decision d; d.part=&plan.parts[0]; d.checks=checks; ++marks;
        for (const auto& c:checks) if (!c.passed) {
            status=Status::refused; reasons.push_back("refused: "+c.name+": "+c.detail);
        }
        if (status==Status::refused) {
            d.outcome=Outcome::aborted;
            d.console.push_back("osp: packet "+plan.id+" REFUSED before any step: "+reasons.front());
            d.console.push_back("osp: details are in run.json; nothing was changed. Fix the install and restart the game.");
            return d;
        }
        status=Status::in_progress; next=1; d.outcome=Outcome::advanced;
        d.console.push_back("osp: packet "+plan.id+" v"+std::to_string(plan.version)+" accepted: "+std::to_string(plan.steps())+" steps, "+std::to_string(plan.parts.size())+" parts.");
        d.console.push_back(next_line());
        return d;
    }
    void abort(const std::string& why) {
        if (status!=Status::in_progress) return;
        status=Status::aborted; reasons.push_back(why);
    }
    // Screenshot files that appeared since the previous mark, oldest first.
    void bind(const std::vector<ShotFile>& files) {
        for (const auto& f:files) {
            BoundShot b; b.file=f; b.seen_at_mark=marks;
            if (pending.empty()) b.status="unexpected";
            else { b.step=pending.front().step; b.shot=pending.front().shot; b.status="bound"; pending.erase(pending.begin()); }
            shots.push_back(std::move(b));
        }
    }
    Decision mark(const Observation& o) {
        Decision d; ++marks;
        if (status!=Status::in_progress) {
            d.console.push_back(std::string("osp: the run is ")+status_name(status)+"; nothing recorded. Restart the game for a new run.");
            return d;
        }
        const Part& p=plan.parts[next]; d.part=&p;
        const std::size_t row=marks-1;      // this mark's index in run.json
        auto check=[&](const std::string& name,bool ok,const std::string& detail){ d.checks.push_back({name,ok,detail}); return ok; };
        auto note=[&](const std::string& name,Json value){ d.observed.push_back({name,std::move(value)}); };
        auto stop=[&](const std::string& why){
            abort(why); d.outcome=Outcome::aborted;
            d.console.push_back("osp: RUN ABORTED at "+p.name+": "+why);
            d.console.push_back("osp: restore by hand: leave free camera (tfc), show the interface (tm), collision on (tcl); quit without saving.");
            return d;
        };
        for (unsigned k=0;k<p.shots;++k) pending.push_back({p.step,p.role==Role::repeat?"repeat":"photo",marks});
        SettleClock& clock=clocks_[p.step];
        auto value=[](const std::optional<std::uint32_t>& w)->std::optional<double>{
            return !w || std::isnan(word_value(*w))?std::nullopt:std::optional<double>(word_value(*w)); };
        auto restart=[&](const char* role){
            clock.started=true; clock.wall=o.clock; clock.days=value(o.game_days_bits); clock.origin_mark=row; clock.origin_role=role; };
        auto since=[&](){
            d.wall_since=o.clock-clock.wall;
            auto days=value(o.game_days_bits); auto scale=value(o.timescale_bits);
            if (clock.days && days && scale && *scale>0)
                d.simulated_since=(*days-*clock.days)*86400.0/(*scale);
        };
        auto origin=[&](){ return Json::object({{"mark",clock.origin_mark},{"role",clock.origin_role}}); };
        switch (p.role) {
        case Role::start: return stop("unexpected start part in the middle of a run");
        case Role::note: d.console.push_back("osp: loading the bench cell. Wait until it has loaded, then type the next part."); break;
        case Role::cell: {
            std::uint32_t want=o.bench_index?(std::uint32_t(*o.bench_index)<<24)|plan.cell_local:0;
            if (!check("bench_cell",o.bench_index && o.parent_cell && *o.parent_cell==want,
                       o.parent_cell?"parent cell "+hex(*o.parent_cell)+", expected "+hex(want):"no parent cell"))
                return stop("the player is not in the bench cell");
            d.console.push_back("osp: bench cell confirmed. The console should now say Collision -> Off.");
            break;
        }
        case Role::pose: {
            const Part& settle=plan.parts[next+1];
            clock=SettleClock{}; restart("pose");
            note("pose_in_effect_at_pose_mark",o.position?Json(within(*o.position,settle.position,settle.tolerance)):Json());
            note("timing_origin",origin());
            d.console.push_back("osp: step "+std::to_string(step_number(p))+"/"+std::to_string(plan.steps())+" "+p.step+": pose sent; settle timing starts at this mark.");
            d.console.push_back("osp: close the console, wait "+std::to_string(settle.settle_seconds)+" s (game time runs only while it is closed), open it and type the next part.");
            break;
        }
        case Role::settle: {
            d.read_camera=true;
            bool readable=o.position.has_value();
            bool here=readable && within(*o.position,p.position,p.tolerance);
            if (readable && !here) for (const auto& q:plan.parts)
                if (q.role==Role::settle && q.step!=p.step && within(*o.position,q.position,q.tolerance) && !within(q.position,p.position,p.tolerance))
                    return stop("the player stands at step "+q.step+"'s pose: parts typed out of order");
            if (!clock.started) restart("settle");   // defensive: no pose mark seen for this step
            since();
            note("pose_in_effect",readable?Json(here):Json());
            note("timing_origin",origin());
            bool wall_ok=*d.wall_since>=p.settle_seconds;
            bool sim_ok=!p.simulated_required || !d.simulated_since || *d.simulated_since>=p.settle_seconds;
            check("position",here,here?"within tolerance":readable?"position differs from the request; the step will not qualify":"position unreadable; the step will not qualify");
            check("wall_seconds",wall_ok,fixed(*d.wall_since)+" of "+std::to_string(p.settle_seconds)+" since mark "+std::to_string(clock.origin_mark));
            check("simulated_seconds",sim_ok,d.simulated_since?fixed(*d.simulated_since)+" of "+std::to_string(p.settle_seconds):"game time unavailable; recorded as null");
            auto wait_again=[&](const std::string& first){
                d.outcome=Outcome::not_settled;
                d.console.push_back(first);
                d.console.push_back("osp: close the console, wait, open it and type the same part again.");
                d.console.push_back("osp: next: bat "+p.name);
                return d;
            };
            if (readable && !here && ++clock.misses==1) {
                restart("settle");
                note("timing_restarted",true);
                return wait_again("osp: the pose is not in effect at this mark; settle timing restarts here (wait "+std::to_string(p.settle_seconds)+" s).");
            }
            if (!wall_ok || !sim_ok)
                return wait_again("osp: not settled yet ("+fixed(*d.wall_since)+" s clock, "+(d.simulated_since?fixed(*d.simulated_since):std::string("n/a"))+" s game, need "+std::to_string(p.settle_seconds)+").");
            if (!here) d.console.push_back("osp: WARNING position is not the requested pose; this step will not qualify.");
            d.console.push_back("osp: settled ("+fixed(*d.wall_since)+" s clock, "+(d.simulated_since?fixed(*d.simulated_since):std::string("n/a"))+" s game since mark "+std::to_string(clock.origin_mark)+").");
            break;
        }
        case Role::free:
            free_clock_[p.step]=o.clock;
            d.console.push_back("osp: free camera requested. Close the console, wait "+std::to_string(plan.parts[next+1].free_seconds)+" s, open it and type the next part.");
            break;
        case Role::capture: {
            d.read_camera=true; d.read_roots=true;
            if (!check("free_camera",o.camera_complete && o.camera_state && *o.camera_state==3,
                       o.camera_state?"camera state "+std::to_string(*o.camera_state):"camera unreadable"))
                return stop("free camera not established before capture");
            auto f=free_clock_.find(p.step);
            if (f!=free_clock_.end()) d.wall_since=o.clock-f->second;
            check("free_seconds",d.wall_since && *d.wall_since>=p.free_seconds,d.wall_since?fixed(*d.wall_since):"no free mark");
            d.console.push_back("osp: camera and roots recorded; this part hides the interface. Type the next three parts blind.");
            break;
        }
        case Role::photo:
            d.read_camera=true;
            d.console.push_back("osp: photograph requested.");
            break;
        case Role::repeat:
            d.read_camera=true;
            d.console.push_back("osp: repeat requested.");
            break;
        case Role::restore:
            d.read_camera=true;
            d.console.push_back("osp: interface and camera restore requested; the console should be visible again.");
            break;
        case Role::end:
            status=Status::complete; next=plan.parts.size(); d.outcome=Outcome::advanced;
            d.console.push_back("osp: run complete. Quit the game without saving, then run osp_packet.py analyze on the run folder.");
            return d;
        }
        ++next; d.outcome=Outcome::advanced;
        if (expected()) d.console.push_back(next_line());
        return d;
    }
    std::size_t step_number(const Part& p) const {
        std::size_t n=0; for (const auto& q:plan.parts) { n+=q.role==Role::pose; if (&q==&p) break; } return n;
    }
};

// Fill the observation's camera state from the DLL's camera serialization
// (osp::scene::camera: {"complete": bool, "state_id": unsigned or null, ...}).
inline void apply_camera(const Json& camera,Observation& o) {
    o.camera_state.reset(); o.camera_complete=false;
    auto object=std::get_if<Json::Object>(&camera.data);
    if (!object) return;
    for (const auto& [key,value]:*object) {
        if (key=="complete") { auto b=std::get_if<bool>(&value.data); o.camera_complete=b && *b; }
        if (key=="state_id") {
            if (auto u=std::get_if<std::uint64_t>(&value.data); u && *u<=0xffffffffu) o.camera_state=std::uint32_t(*u);
            if (auto i=std::get_if<std::int64_t>(&value.data); i && *i>=0 && *i<=0xffffffffll) o.camera_state=std::uint32_t(*i);
        }
    }
}

// ------------------------------------------------------------- run.json
inline Json checks_json(const std::vector<Check>& checks) {
    Json::Array a;
    for (const auto& c:checks) a.push_back(Json::object({{"name",c.name},{"passed",c.passed},{"detail",c.detail}}));
    return a;
}
inline Json word_json(std::uint32_t bits) {
    Json decimal; double v=word_value(bits); if (!std::isnan(v)) decimal=Json(v);
    return Json::object({{"bits",hex(bits)},{"decimal",decimal}});
}
inline Json optional_number(const std::optional<double>& v) { return v && std::isfinite(*v)?Json(*v):Json(); }
// One row per mark. Camera and roots are the DLL's raw serializations; scan is
// the DLL's record of the screenshot folder at this mark (files left for later).
inline Json mark_json(std::size_t index,const Decision& d,const Observation& o,const std::string& utc,
                      Json camera,Json roots,const std::vector<std::string>& printed,Json scan=Json()) {
    Json::Array position,messages; Json::Object observed;
    if (o.position) for (auto w:*o.position) position.push_back(word_json(w));
    for (const auto& m:printed) messages.emplace_back(m);
    for (const auto& [k,v]:d.observed) observed.push_back({k,v});
    return Json::object({{"index",index},{"part",d.part?Json(d.part->name):Json()},
        {"role",d.part?Json(role_name(d.part->role)):Json()},{"step",d.part?Json(d.part->step):Json()},
        {"outcome",outcome_name(d.outcome)},{"utc",utc},{"qpc_seconds",o.clock},
        {"wall_seconds_since_pose",optional_number(d.wall_since)},
        {"simulated_seconds_since_pose",optional_number(d.simulated_since)},
        {"game_days_passed",o.game_days_bits?word_json(*o.game_days_bits):Json()},
        {"timescale",o.timescale_bits?word_json(*o.timescale_bits):Json()},
        {"player_position",o.position?Json(position):Json()},
        {"player_parent_cell",o.parent_cell?Json(hex(*o.parent_cell)):Json()},
        {"camera",std::move(camera)},{"roots",std::move(roots)},{"checks",checks_json(d.checks)},
        {"observed",Json::object(std::move(observed))},{"screenshot_scan",std::move(scan)},
        {"messages",messages}});
}
inline Json shots_json(const Run& run) {
    Json::Array a;
    for (const auto& s:run.shots)
        a.push_back(Json::object({{"file",s.file.name},{"size",s.file.size},{"sha256",s.file.sha256},
            {"bound_to",s.status=="bound"?Json::object({{"step",s.step},{"shot",s.shot}}):Json()},
            {"status",s.status},{"seen_at_mark",s.seen_at_mark}}));
    return a;
}
inline Json pending_json(const Run& run) {
    Json::Array a;
    for (const auto& p:run.pending) a.push_back(Json::object({{"step",p.step},{"shot",p.shot},{"requested_at_mark",p.requested_at_mark}}));
    return a;
}
// run_schema 2: photo_v2 roles, settle timing from the pose mark, per-mark
// observations and screenshot scans, and the F4SE messages the plugin received.
constexpr int run_schema=2;
inline Json run_json(const Run& run,const std::string& plan_sha256,const std::string& session,const std::string& run_id,
                     const std::string& started,const std::string& updated,Json environment,Json preflight_checks,Json marks,
                     Json f4se_messages=Json::Array{}) {
    Json::Array reasons; for (const auto& r:run.reasons) reasons.emplace_back(r);
    auto next=run.expected();
    return Json::object({{"run_schema",run_schema},{"plugin_version",9},{"status",status_name(run.status)},
        {"status_reasons",reasons},
        {"packet",Json::object({{"id",run.plan.id},{"version",run.plan.version},{"sha256",run.plan.packet_sha256},{"plan_sha256",plan_sha256}})},
        {"session_id",session},{"run_id",run_id},{"started_utc",started},{"updated_utc",updated},
        {"environment",std::move(environment)},{"preflight",Json::object({{"checks",std::move(preflight_checks)}})},
        {"marks",std::move(marks)},{"f4se_messages",std::move(f4se_messages)},
        {"screenshots",shots_json(run)},{"pending_screenshots",pending_json(run)},
        {"next_part",next?Json(next->name):Json()}});
}
}
