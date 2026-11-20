// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/observe.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <array>

struct Memory {
    std::array<unsigned char,16> bytes{};
    bool read(std::uint64_t p,void* out,std::size_t n) {
        if (p<0x1000 || p-0x1000>bytes.size() || n>bytes.size()-(p-0x1000)) return false;
        std::memcpy(out,bytes.data()+(p-0x1000),n); return true;
    }
};
int main() {
    using namespace osp;
    assert(!address_range(0,1)); assert(!address_range(1,0));
    assert(address_range(UINT64_MAX,1)); assert(!address_range(UINT64_MAX,2));
    assert(!valid_array({0,1,0,1,0})); assert(valid_array({0,0,0,0,0}));
    assert(!valid_array({0x1000,1,0,2,0}));
    assert(!valid_array({UINT64_MAX-3,1,0,1,0}));
    assert(!valid_array({0x1000,1000001,0,0,0}));
    assert(valid_array({0x1000,2,0,2,0}));
    Memory memory;
    memory.bytes.fill('x'); memory.bytes[15]=0;
    auto text=read_string(memory,0x1000);
    assert(text && *text==std::string(15,'x'));
    assert(read_string(memory,0x100f).value()=="");
    assert(!read_string(memory,0)); assert(!read_string(memory,0x1010));
    assert(!read_string(memory,0x1000,15));
    memory.bytes[15]='x'; assert(!read_string(memory,0x1000));
    assert((!read_at<Memory,std::uint64_t>(memory,0x100c)));
    assert((!read_at<Memory,std::uint64_t>(memory,UINT64_MAX)));
    assert(packed_version(0x010a0a30)=="1.10.163.0");
    assert(compatible(0x010a0a30,0x60170,0,25));
    assert(!compatible(0x010a0a20,0x60170,0,25));
    assert(!compatible(0x010a0a30,0x60160,0,25));
    assert(!compatible(0x010a0a30,0x60170,1,25));
    assert(!compatible(0x010a0a30,0x60170,0,24));
    assert(trigger(3,1,1)==Trigger::loaded);
    assert(trigger(3,0,1)==Trigger::none);
    assert(trigger(3,0x1000,1)==Trigger::none); // Never dereference a bool payload.
    assert(trigger(3,1,0)==Trigger::none);
    assert(trigger(8,0x1000,8)==Trigger::new_game);
    assert(trigger(9,0,0)==Trigger::none); // GameLoaded is not a per-save event.
    assert(trigger(2,0,0)==Trigger::none);

    Json::Array settings;
    settings.push_back(decode_setting("iNegative",0xffffffff,std::nullopt));
    settings.push_back(decode_setting("bLowByte",0x100,std::nullopt));
    settings.push_back(decode_setting("bTrue",0x80,std::nullopt));
    settings.push_back(decode_setting("fValue",0x3fa00000,std::nullopt));
    settings.push_back(decode_setting("sText",0,std::string("quote\" slash\\\n")));
    settings.push_back(decode_setting("SNull",0,std::nullopt));
    settings.push_back(decode_setting("rID6",0x12345678,std::nullopt));
    settings.push_back(decode_setting("aID",0xffffffff,std::nullopt));
    settings.push_back(decode_setting("uUnknown",0x42,std::nullopt));
    settings.push_back(decode_setting("",0,std::nullopt));
    settings.push_back(decode_setting("fNaN",0x7fc00000,std::nullopt));

    Json::Array strings;
    for (int c=0;c<256;++c) strings.emplace_back(std::string(1,char(c)));
    strings.emplace_back(u8"Medição — 😀");
    strings.emplace_back(std::string("\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80",9));
    strings.emplace_back(std::string("\xe2\x82",2));
    // Deterministic fuzz corpus exercises malformed UTF-8 and arbitrary controls.
    std::uint32_t seed=0x12345678;
    for (int i=0;i<4096;++i) {
        std::string s;
        for (int j=0;j<i%61;++j) { seed=1664525*seed+1013904223; s.push_back(char(seed>>24)); }
        strings.emplace_back(s);
    }
    Json root=Json::object({{"settings",settings},{"strings",strings},
        {"numbers",Json::Array{Json(INT64_MIN),Json(UINT64_MAX),Json(-0.0),Json(1.0/3.0),
            Json(std::numeric_limits<double>::denorm_min()),Json(std::numeric_limits<double>::max()),
            Json(std::numeric_limits<double>::infinity()),Json(-std::numeric_limits<double>::infinity()),
            Json(std::numeric_limits<double>::quiet_NaN())}},
        {"nested",Json::object({{"empty_array",Json::Array{}},{"empty_object",Json::object({})},
                               {"null",nullptr},{"true",true},{"false",false}})}});
    std::cout << encode(root);
}
