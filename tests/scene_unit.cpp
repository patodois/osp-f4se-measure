// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/scene.hpp"
#include "osp/settings.hpp"
#include <cassert>
#include <map>
#include <iostream>
#undef assert
#define assert(expression) do { if(!(expression)) {std::cerr<<"failed line "<<__LINE__<<"\n";return 1;} } while(false)
using osp::layout::Address;
struct Memory {
    std::map<Address,unsigned char> bytes;
    bool read(Address a,void* out,std::size_t n) {
        auto b=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<n;++i) {auto at=bytes.find(a+i);if(at==bytes.end()) return false;b[i]=at->second;}
        return true;
    }
    template<class T> void put(Address a,const T& t) {
        auto b=reinterpret_cast<const unsigned char*>(&t);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[a+i]=b[i];
    }
    void text(Address a,const char* s) {for(std::size_t i=0;i<=std::strlen(s);++i) bytes[a+i]=s[i];}
};
struct Calls {
    std::optional<Address> get(Address,std::size_t slot) {return slot==2?std::optional<Address>(0x4000):slot==3||slot==8?std::optional<Address>(0):std::nullopt;}
};
int main() {
    namespace s=osp::scene;namespace n=osp::nodes;Memory m;
    assert(s::request("test\nnodes2=00000014\n")->form_id==20);
    assert(!s::request("test\nnodes=00000014\n"));assert(!s::request("test\nnodes2=0\n"));
    assert(!n::request("test\nnodes2=00000014\n"));
    m.put(0x1140,std::int32_t(2));m.put(0x1144,std::int32_t(1));m.put(0x1148,Address(0x2000));
    n::Transform t{};t.words[0]=t.words[5]=t.words[10]=t.words[15]=0x3f800000;t.words[3]=0x7f800001;
    s::Bone root{};root.parent=root.child=root.sibling=-1;root.local=root.world=t;root.name=0x3000;
    m.put(0x3000,n::StringEntry{0,1,4,0});m.text(0x3018,"root");
    auto child=root;child.parent=0;child.name=0x3100;child.node=0x6000;m.put(0x3100,n::StringEntry{0,1,5,0});m.text(0x3118,"child");
    m.put(0x2000,root);m.put(0x20a0,child);
    auto valid=s::flattened(m,0x1000);assert(osp::flag(valid,"complete"));std::cout<<osp::encode(valid);
    osp::Json::Array many;
    for(unsigned i=0;i<12;++i) many.push_back(osp::Json::object({{"type","BSFlattenedBoneTree"},{"address",osp::hex(0x1000,16)}}));
    auto bounded=s::flattened_trees(m,osp::Json::object({{"nodes",many}}));
    auto candidates=std::get<osp::Json::Array>(bounded.data);
    assert(candidates.size()==9 && !osp::flag(candidates.back(),"complete"));
    root.parent=1;m.put(0x2000,root);assert(!osp::flag(s::flattened(m,0x1000),"complete"));root.parent=-1;m.put(0x2000,root);
    assert(!osp::flag(s::flattened(m,0),"complete"));assert(!osp::flag(s::flattened(m,UINT64_MAX-4),"complete"));
    assert(!osp::flag(s::flattened(m,0x1000,1),"complete"));
    m.put(0x1140,std::int32_t(-1));assert(!osp::flag(s::flattened(m,0x1000),"complete"));m.put(0x1140,std::int32_t(2));
    m.put(0x1148,Address(0xfffffffffffffff0));assert(!osp::flag(s::flattened(m,0x1000),"complete"));m.put(0x1148,Address(0x2000));
    child.name=0x8000;m.put(0x20a0,child);assert(!osp::flag(s::flattened(m,0x1000),"complete"));
    Calls calls;assert(!osp::flag(s::camera(m,calls,0),"complete"));
    m.put(0x100000000+s::camera_rva,Address(0x7000));m.put(0x7020,Address(0x6000));m.put(0x7028,Address(0x7800));m.put(0x7820,std::uint32_t(3));m.put(0x7168,std::uint32_t(0x42b40000));m.put(0x716c,std::uint32_t(0x42800000));
    n::Object camera{};camera.local=camera.world=t;m.put(0x6000,camera);m.put(0x4000,Address(0x4100));m.text(0x4100,"NiCamera");
    m.put(0x6120,std::array<std::uint32_t,16>{});m.put(0x6160,std::array<std::uint32_t,6>{});m.put(0x6178,std::uint8_t(0));m.put(0x6184,std::array<std::uint32_t,4>{});
    auto cam=s::camera(m,calls,0x100000000);assert(osp::flag(cam,"complete"));std::cout<<osp::encode(cam);
    m.put(0x6178,std::uint8_t(2));assert(!osp::flag(s::camera(m,calls,0x100000000),"complete"));
}
