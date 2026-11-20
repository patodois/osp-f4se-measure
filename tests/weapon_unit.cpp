// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/weapon.hpp"
#include <cassert>
#include <iostream>
#include <limits>
struct Memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x20000);
    bool read(std::uint64_t p,void* out,std::size_t n) {
        if(p<0x1000 || p-0x1000>bytes.size() || n>bytes.size()-(p-0x1000))return false;
        std::memcpy(out,bytes.data()+p-0x1000,n);return true;
    }
    template<class T> void put(std::uint64_t p,T v){assert(p>=0x1000 && p-0x1000+sizeof(v)<=bytes.size());std::memcpy(bytes.data()+p-0x1000,&v,sizeof(v));}
};
int main(){
    using namespace osp;using namespace osp::weapon;
    Memory m;Json::Array errors;
    assert(encode(observe(m,0,errors)).find("unavailable_or_ambiguous")!=std::string::npos);
    m.put<std::uint64_t>(0x1300,0x3000);m.put<std::uint64_t>(0x3008,0x4000);
    m.put(0x4288,layout::Array{0x5000,1,0,1,0});m.put(0x5000,Equip{0x6000,0x7000,0,0,0});
    layout::Form form{};form.type=43;form.id=0xabcdef;m.put(0x6000,form);
    Instance v{};v.ammo=0x8000;v.aim=0x9000;v.zoom=0xa000;
    v.reload_speed=1.25f;v.speed=2;v.min_range=125;v.max_range=2400;v.attack_delay=.375f;
    v.out_of_range=.5f;v.capacity=17;v.damage=63;v.accuracy_bonus=75;v.flags=0x404000;m.put(0x7000,v);
    form.id=0x123456;form.type=44;m.put(0x8000,form);form.id=0x3456;m.put(0x9000,form);form.id=0x4567;m.put(0xa000,form);
    m.put<std::uint64_t>(0x1428,0xb000);m.put(0xb010,Slot{0x6000,0x7000,0xc000,{}});
    Extra extra{};extra.type=0x35;extra.data=0xd000;m.put(0xc000,extra);m.put(0xd000,ModBlock{0xe000,16,0});
    m.put<std::uint32_t>(0xe000,0x1111);m.put<std::uint32_t>(0xe008,0x2222);
    errors.clear();auto s=encode(observe(m,0x1000,errors));assert(errors.empty());
    for(auto text:{"\"min_range\":125","\"max_range\":2400","\"reload_speed\":1.25","\"capacity\":17","\"accuracy_bonus\":75","\"base_damage\":63","\"automatic_sdk_flag\":true","\"bolt_action_sdk_flag\":true","00001111","00002222","\"fields\":null","\"fov\":null"})assert(s.find(text)!=std::string::npos);
    v.ammo=UINT64_MAX-3;m.put(0x7000,v);errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("\"instance_complete\":false")!=std::string::npos);
    v.ammo=0x8000;m.put(0x7000,v);
    m.put<std::uint64_t>(0x1b70,0xb000);m.put(0xb068,Slot{0x6000,0x7000,0xc100,{}});m.put(0xc100,extra);
    errors.clear();s=encode(observe(m,0x1000,errors));assert(errors.empty()&&s.find("\"candidate_objects\":2")!=std::string::npos);
    Extra different=extra;different.data=0xd100;m.put(0xc100,different);m.put(0xd100,ModBlock{0xe000,8,0});
    errors.clear();observe(m,0x1000,errors);assert(!errors.empty());m.put(0xb068,Slot{});
    // Distinct runtime/record Automatic bits must not be conflated.
    v.flags=0x8000;m.put(0x7000,v);s=encode(observe(m,0x1000,errors));assert(s.find("\"automatic_sdk_flag\":false")!=std::string::npos);
    v.min_range=std::numeric_limits<float>::quiet_NaN();m.put(0x7000,v);errors.clear();s=encode(observe(m,0x1000,errors));assert(!errors.empty()&&s.find("\"min_range\":null")!=std::string::npos);
    m.put(0x5000,Equip{0x6000,0,0,0,0});errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("\"instance_complete\":false")!=std::string::npos);
    m.put(0x4288,layout::Array{0x5000,2,0,2,0});m.put(0x5028,Equip{0x6000,0x7000,0,0,0});errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("ambiguous")!=std::string::npos);
    m.put(0x4288,layout::Array{UINT64_MAX-8,1,0,1,0});assert(encode(observe(m,0x1000,errors)).find("ambiguous")!=std::string::npos);
    // Fallback equipped inventory stack: reject cycles and malformed mod blocks.
    m.put(0x4288,layout::Array{0x5000,1,0,1,0});m.put(0x5000,Equip{0x6000,0x7000,0,0,0});m.put<std::uint64_t>(0x1428,0);m.put<std::uint64_t>(0x1b70,0);
    m.put<std::uint64_t>(0x10f8,0xf000);m.put(0xf058,layout::Array{0x10000,1,0,1,0});m.put(0x10000,InventoryItem{0x6000,0x11000});
    m.put(0x11000,Stack{{},0,0x12000,1,7,0});m.put<std::uint64_t>(0x12008,0xc000);
    v.min_range=125;m.put(0x7000,v);errors.clear();s=encode(observe(m,0x1000,errors));assert(errors.empty()&&s.find("00002222")!=std::string::npos&&s.find("items_count")!=std::string::npos);
    m.put(0x11000,Stack{{},0x11000,0x12000,1,7,0});errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("inventory stack cycle or cap")!=std::string::npos && s.find("\"mods\":{\"complete\":false")!=std::string::npos);
    m.put(0x11000,Stack{{},0,0x12000,1,7,0});
    extra.next=0xc000;m.put(0xc000,extra);errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("extra-data cycle or cap")!=std::string::npos && s.find("\"mods\":{\"complete\":false")!=std::string::npos);
    extra.next=0;m.put(0xc000,extra);
    m.put(0xd000,ModBlock{0xe000,9,0});errors.clear();observe(m,0x1000,errors);assert(!errors.empty());
    m.put(0xd000,ModBlock{0xe000,16,0});
    m.put(0x11000,Stack{{},0x11000,0,1,0,0});errors.clear();s=encode(observe(m,0x1000,errors));assert(s.find("inventory stack cycle or cap")!=std::string::npos);
    std::cout<<"PASS: equipped instance fields, ABI flags, nulls, ambiguity, finite values, bounded inventory/mod lists\n";
}
