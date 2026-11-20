// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/settings.hpp"
#include <array>
#include <cassert>
#include <iostream>

using osp::layout::Address;
struct Memory {
    std::array<char,1024> bytes{};
    bool read(Address p,void* out,std::size_t n) {
        if (p<0x1000 || p-0x1000>bytes.size() || n>bytes.size()-(p-0x1000)) return false;
        std::memcpy(out,bytes.data()+p-0x1000,n); return true;
    }
    void setting(const std::string& name,Address bits) {
        bytes.fill(0);
        osp::layout::Setting value{0,bits,0x1100};
        std::memcpy(bytes.data(),&value,sizeof(value));
        std::memcpy(bytes.data()+0x100,name.c_str(),name.size()+1);
    }
};
struct Calls {
    bool ctor_ok=true, key_ok=true, find_ok=true, release_ok=true, rebind=false;
    int finds=0, releases=0;
    Address found=0x1000, other_slot=0;
    bool construct(osp::layout::FixedString* key,const char* text) {
        assert(text && *text); key->entry=key_ok?0xdead:0; return ctor_ok;
    }
    bool find(Address map,osp::layout::FixedString* key,Address** output) {
        assert(map==0x2000 && key->entry==0xdead); ++finds;
        **output=found;
        if (rebind) *output=&other_slot;
        return find_ok;
    }
    bool release(osp::layout::FixedString*) { ++releases; return release_ok; }
};
int main() {
    using namespace osp;
    auto names=parse_settings_names("\xef\xbb\xbf  # comment\r\nfSynthetic\r\n\n iSynthetic \t\nfSynthetic\nuSynthetic");
    assert((names.names==std::vector<std::string>{"fSynthetic","iSynthetic","uSynthetic"}));
    assert(names.duplicates==1 && names.errors.empty());
    assert(settings_coverage_complete(names,3));
    assert(!settings_coverage_complete(names,2) && !settings_coverage_complete(names,4));
    auto invalid=names; invalid.errors.push_back("bad input");
    assert(!settings_coverage_complete(invalid,3));
    assert(!settings_coverage_complete(SettingsNames{},0));
    assert(!parse_settings_names("").errors.empty());
    assert(!parse_settings_names("bad name\n").errors.empty());
    assert(!parse_settings_names(std::string("fA\0bad",6)).errors.empty());
    assert(!parse_settings_names("\xff\xfe").errors.empty());
    assert(!parse_settings_names(std::string(max_names_bytes+1,'x')).errors.empty());
    Memory memory; Calls calls;
    auto lookup=lookup_setting(calls,0x2000,"fSynthetic");
    assert(lookup.queried && lookup.setting==0x1000 && calls.finds==1 && calls.releases==1);
    calls={}; auto missing_map=lookup_setting(calls,0,"fSynthetic");
    assert(!missing_map.queried && calls.releases==0 && calls.finds==0);
    calls={}; calls.ctor_ok=false;
    assert(!lookup_setting(calls,0x2000,"fSynthetic").queried && calls.releases==0);
    calls={}; calls.key_ok=false;
    assert(!lookup_setting(calls,0x2000,"fSynthetic").queried && calls.finds==0 && calls.releases==1);
    calls={}; calls.find_ok=false;
    assert(!lookup_setting(calls,0x2000,"fSynthetic").queried && calls.releases==1);
    calls={}; calls.rebind=true;
    assert(!lookup_setting(calls,0x2000,"fSynthetic").queried && calls.releases==1);
    calls={}; calls.release_ok=false;
    auto release_fault=lookup_setting(calls,0x2000,"fSynthetic");
    assert(release_fault.queried && !release_fault.errors.empty());

    Json::Array rows;
    memory.setting("fSynthetic",0x3fc00000);
    auto item=observe_setting(memory,"fSynthetic",lookup);
    assert(item.queried && item.found && item.value_read && item.errors.empty());
    rows.push_back(item.json);
    memory.setting("SLocalText",0x1200);
    std::string text=u8"Synthetic \"text\" — 😀";
    std::memcpy(memory.bytes.data()+0x200,text.c_str(),text.size()+1);
    item=observe_setting(memory,"sLocalText",lookup);
    assert(item.value_read && item.errors.empty()); rows.push_back(item.json);
    memory.setting("sNull",0);
    item=observe_setting(memory,"sNull",lookup);
    assert(item.found && !item.value_read && !item.errors.empty()); rows.push_back(item.json);
    memory.setting("fNonfinite",0x7f800000);
    item=observe_setting(memory,"fNonfinite",lookup);
    assert(item.found && !item.value_read && !item.errors.empty()); rows.push_back(item.json);
    memory.setting("uRaw",0xffffffff);
    item=observe_setting(memory,"uRaw",lookup);
    assert(item.value_read && !item.errors.empty()); rows.push_back(item.json);
    item=observe_setting(memory,"iAbsent",SettingLookup{true,0,{}});
    assert(item.queried && !item.found && !item.errors.empty()); rows.push_back(item.json);
    item=observe_setting(memory,"iFault",SettingLookup{false,0,{"GetSetting call fault"}});
    assert(!item.queried && !item.found); rows.push_back(item.json);
    item=observe_setting(memory,"iUnreadable",SettingLookup{true,0x9999,{}});
    assert(item.queried && item.found && !item.value_read); rows.push_back(item.json);
    std::cout<<encode(Json(rows));
}
