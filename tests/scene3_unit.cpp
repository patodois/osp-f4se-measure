// SPDX-License-Identifier: MIT OR Apache-2.0
#include "osp/scene3.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::cerr<<"check failed line "<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
using namespace osp;using namespace osp::scene3;
Transform identity(){Transform t{};t.words[0]=t.words[5]=t.words[10]=t.words[15]=0x3f800000;return t;}
struct Reader {
 std::vector<unsigned char> bytes=std::vector<unsigned char>(0x10000);
 bool read(Address a,void*p,std::size_t n){if(a<0x1000 || n>bytes.size() || a-0x1000>bytes.size()-n)return false;std::memcpy(p,bytes.data()+a-0x1000,n);return true;}
 template<class T>void put(Address a,const T&v){std::memcpy(bytes.data()+a-0x1000,&v,sizeof(v));}
 void name(Address a,const char*s){put(a,nodes::StringEntry{0,1,static_cast<std::uint32_t>(std::strlen(s)),0});std::memcpy(bytes.data()+a-0x1000+0x18,s,std::strlen(s)+1);}
};
struct Calls {int constructions=0,releases=0;bool fail=false,release_fail=false,partial=false;Address entry=0x8000;
 bool construct(layout::FixedString*k,const char*){++constructions;k->entry=(!fail || partial)?entry:0;return !fail;}
 bool release(layout::FixedString*){++releases;return !release_fail;}
};
std::string bytes_forecast(){std::string out="OSPB19V1";auto number=[&](unsigned v,unsigned n){for(unsigned i=0;i<n;++i)out.push_back(char(v>>(8*i)));};auto text=[&](const std::string&s){number(unsigned(s.size()),2);out+=s;};number(2,4);number(2,4);for(int view=0;view<2;++view){text("synthetic-root");number(0xffffffffu,4);text("SPINE");number(0,4);text("child");}return out;}
struct Fixture {
 Reader reader;Calls calls;View expected;Json ordinary;
 Fixture(){
  nodes::Object root{};root.name=0x9000;root.world=identity();root.world.words[12]=0x41200000;reader.put(0x2000,root);reader.name(0x9000,"synthetic-root");reader.name(0x8000,"Spine");reader.name(0x8800,"child");
  reader.put(0x2140,std::int32_t(2));reader.put(0x2144,std::int32_t(1));reader.put(0x2148,Address(0x5000));
  nodes::Object node{};node.name=0x8000;node.local=identity();node.local.words[12]=0x40800000;node.world=compose(root.world,node.local,true);reader.put(0x3000,node);
  scene::Bone a{};a.local=identity();a.world=node.world;a.parent=-1;a.child=1;a.sibling=-1;a.child_count=1;a.node=0x3000;a.name=0x8000;reader.put(0x5000,a);
  scene::Bone b{};b.local=identity();b.local.words[13]=0x40000000;b.world=compose(a.world,b.local,true);b.parent=0;b.child=b.sibling=-1;b.name=0x8800;reader.put(0x50a0,b);
  expected=forecast(bytes_forecast()).third;ordinary=Json::object({{"complete",true},{"transforms_and_hierarchy_complete",true},{"nodes",Json::Array{Json::object({{"address",hex(0x2000,16)}}),Json::object({{"address",hex(0x3000,16)}})}}});
 }
 Proof run(Json* output=nullptr){auto f=scene::flattened(reader,0x2000);auto proof=enrich(reader,calls,f,ordinary,expected);if(output)*output=f;return proof;}
};
int main(){
 CHECK(request("tag\nnodes3=00000014\npause=00000001\n")->pause==1);CHECK(!request("tag\nnodes3=00000014\npause=00000000\n"));CHECK(!request("tag\nnodes3=00000014 argument\npause=00000001\n"));CHECK(!request("tag\nnodes3=00000000\npause=00000001\n"));
 auto encoded=bytes_forecast();CHECK(forecast(encoded).complete);CHECK(!forecast(encoded+"x").complete);CHECK(!forecast(encoded.substr(0,encoded.size()-1)).complete);encoded[8]=0;CHECK(!forecast(encoded).complete);
 CHECK(add(0x3f800000,0x33800000,false)==0x3f800000);CHECK(add(0x3f800001,0x33800000,false)==0x3f800002);
 CHECK(mul(0x00800000,0x3f000000,false)==0x00400000);CHECK(mul(0x80800000,0x3f000000,true)==0x80000000);CHECK(mul(0x00800000,0x3f000000,true)==0);
 CHECK(rounded(value(1),false)==1);CHECK(rounded(value(0x80000001),false)==0x80000001);
 std::uint32_t seed=0x12345678;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(unsigned i=0;i<20000;++i){auto a=random(),b=random();if((a&0x7f800000)==0x7f800000 || (b&0x7f800000)==0x7f800000)continue;float x,y;std::memcpy(&x,&a,4);std::memcpy(&y,&b,4);volatile float sum=x+y,product=x*y;float s=sum,p=product;std::uint32_t sw,pw;std::memcpy(&sw,&s,4);std::memcpy(&pw,&p,4);CHECK(add(a,b,false)==sw);CHECK(mul(a,b,false)==pw);}
 Fixture good;Json out;auto proof=good.run(&out);CHECK(proof.accepted());CHECK(proof.strict);CHECK(good.calls.constructions==1 && good.calls.releases==1);std::cout<<encode(out);
 for(int flaw=0;flaw<8;++flaw){Fixture f;if(flaw==0)f.calls.entry=0x8888;if(flaw==1)f.calls.fail=true;if(flaw==2)f.calls.release_fail=true;if(flaw==3)f.expected.bones[1].parent=-1;if(flaw==4)f.expected.bones[1].name="unexpected";if(flaw==5){scene::Bone b{};f.reader.read(0x5000,&b,sizeof(b));b.node=0xf000;f.reader.put(0x5000,b);}if(flaw==6){nodes::Object n{};f.reader.read(0x3000,&n,sizeof(n));n.world.words[3]=1;f.reader.put(0x3000,n);}if(flaw==7){f.calls.fail=true;f.calls.partial=true;}CHECK(!f.run().accepted());CHECK(f.calls.releases==f.calls.constructions-(flaw==1?1:0));}
 Fixture raw_flag;scene::Bone flagged{};raw_flag.reader.read(0x5000,&flagged,sizeof(flagged));flagged.deferred_attach=255;raw_flag.reader.put(0x5000,flagged);auto old=scene::flattened(raw_flag.reader,0x2000);CHECK(!flag(old,"complete"));CHECK(enrich(raw_flag.reader,raw_flag.calls,old,raw_flag.ordinary,raw_flag.expected).accepted());
 Tracker tracker;CHECK(!tracker.observe("F",1,true,true,true));CHECK(!tracker.observe("F",1,true,true,true));CHECK(tracker.observe("F",2,true,true,true));CHECK(!tracker.observe("F",3,true,false,false));CHECK(tracker.pauses.empty());CHECK(!tracker.observe("F",4,true,true,true));CHECK(tracker.observe("F",5,true,true,true));CHECK(!tracker.observe("OTHER",6,false,true,true));CHECK(tracker.pauses.empty());
 std::cerr<<"PASS: schema3 framing, checked hybrid proofs/string-key lifetime, software IEEE rounding/FTZ, distinct-pause validation gate\n";
}
