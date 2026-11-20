// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/nodes.hpp"
#include "osp/settings.hpp"
#include <cassert>
#include <iostream>
#include <map>
using osp::layout::Address;
struct Memory {
    std::vector<unsigned char> data=std::vector<unsigned char>(0x10000);
    bool read(Address a,void* out,std::size_t n) {
        if(a<0x1000 || a-0x1000>data.size() || n>data.size()-(a-0x1000)) return false;
        std::memcpy(out,data.data()+a-0x1000,n);return true;
    }
    template<class T> void put(Address a,const T& v){std::memcpy(data.data()+a-0x1000,&v,sizeof(v));}
    void text(Address a,const char* s){std::memcpy(data.data()+a-0x1000,s,std::strlen(s)+1);}
};
struct Calls {
    std::set<Address> nodes,geometries;
    std::optional<Address> get(Address a,std::size_t s) {
        if(s==2) return 0x4000;
        if(s==3) return nodes.count(a)?a:0;
        if(s==8) return geometries.count(a)?a:0;
        return std::nullopt;
    }
};
int main() {
    using namespace osp::nodes;
    assert(request("test\nnodes=00000014\n")->form_id==20);
    assert(request("test\r\nnodes=FF001234\r\n")->form_id==0xff001234);
    for(auto s:{"test\nmode=light\n","test\nnodes=00000000\n","test\nnodes=1234\n","test\nnodes=0000001G\n","test\nnodes=00000014\nextra","bad tag\nnodes=00000014\n"}) assert(!request(s));
    Memory m;Calls c;c.nodes.insert(0x1000);c.geometries.insert(0x2000);
    Transform t{};t.words[0]=t.words[5]=t.words[10]=t.words[15]=0x3f800000;t.words[12]=0x80000000;t.words[3]=0x7fc00001;t.words[7]=0x7f800001;t.words[11]=0xff800000;
    Object root{},leaf{};root.name=0x5000;root.local=root.world=t;leaf=root;leaf.name=0x5100;leaf.parent=0x1000;
    m.put(0x1000,root);m.put(0x2000,leaf);
    m.put(0x5000,StringEntry{0,1,14,0});m.text(0x5018,"synthetic root");
    m.put(0x5100,StringEntry{0,1,7,0});m.text(0x5118,"leaf\"\\");
    m.put(0x5300,StringEntry{0,0x4000,14,0x5000});assert(*fixed_name(m,0x5300)=="synthetic root");
    m.put(0x5400,StringEntry{0,0x8000,3,0});std::u16string wide=u"骨🦴";
    for(std::size_t i=0;i<wide.size();++i)m.put(0x5418+i*2,std::uint16_t(wide[i]));
    m.put(0x5418+wide.size()*2,std::uint16_t(0));assert(*fixed_name(m,0x5400)==u8"骨🦴");
    m.put(0x5418,std::uint16_t(0xdc00));assert(!fixed_name(m,0x5400));
    m.put(0x5300,StringEntry{0,0x4000,0,0x5300});assert(!fixed_name(m,0x5300));
    m.put(0x4000,Address(0x5200));m.text(0x5200,"SyntheticType");
    Children children{0,0x3000,3,3,2,1};m.put(0x1120,children);m.put(0x3000,Address(0x2000));m.put(0x3008,Address(0));m.put(0x3010,Address(0x2800));
    auto other=leaf;other.name=0;m.put(0x2800,other);
    m.put(0x2140,Address(0x6000));osp::layout::Array bones{0x6100,1,0,1,0};m.put(0x6010,bones);m.put(0x6100,Address(0x1000));
    auto valid=tree(m,c,0x1000);assert(osp::flag(valid,"complete"));std::cout<<osp::encode(valid);
    m.put(0x6100,Address(0));auto missing_bone=tree(m,c,0x1000);
    assert(!osp::flag(missing_bone,"complete") && osp::flag(missing_bone,"transforms_and_hierarchy_complete"));
    assert(!osp::flag(missing_bone,"skins_complete"));m.put(0x6100,Address(0x1000));
    m.put(0x3010,Address(0x1000));auto cycle=tree(m,c,0x1000);assert(!osp::flag(cycle,"complete"));std::cout<<osp::encode(cycle);
    assert(!osp::flag(tree(m,c,0x1000,Limits{1,128,8192,2048}),"complete"));
    assert(!osp::flag(tree(m,c,0x1000,Limits{4096,0,8192,2048}),"complete"));
    assert(!osp::flag(tree(m,c,0),"complete"));
    assert(!osp::flag(tree(m,c,UINT64_MAX-10),"complete"));
    assert(!field<Address>(m,UINT64_MAX-5,0x140));
    assert(!osp::flag(tree(m,c,0x1000,Limits{4096,128,8192,2048,0,128}),"complete"));
    assert(!osp::flag(tree(m,c,0x1000,Limits{4096,128,8192,2048,32768,1}),"complete"));
    children.end=4;m.put(0x1120,children);assert(!osp::flag(tree(m,c,0x1000),"complete"));
    children.end=3;m.put(0x1120,children);bones.count=3000;m.put(0x6010,bones);assert(!osp::flag(tree(m,c,0x1000),"complete"));
}
