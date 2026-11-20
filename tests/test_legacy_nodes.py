# SPDX-License-Identifier: MIT OR Apache-2.0
"""Compile the actual v6 serializer with deterministic synthetic observations."""
from pathlib import Path
import hashlib,json,subprocess,sys
from test_legacy_snapshot import function
ROOT=Path(__file__).resolve().parents[1]
PREFIX=r'''
#include "osp/nodes.hpp"
#include "osp/settings.hpp"
#include <iostream>
#include <vector>
using osp::Json;using osp::layout::Address;
int scenario=0;
struct Reader {
 bool read(Address a,void* p,std::size_t n){
  std::vector<unsigned char>b(0x10000);if(a<0x1000||a+n>0x11000)return false;
  auto put=[&](Address at,const auto&v){std::memcpy(b.data()+at-0x1000,&v,sizeof(v));};
  auto text=[&](Address at,const char*v){std::memcpy(b.data()+at-0x1000,v,std::strlen(v)+1);};
  osp::layout::Form f{};f.id=20;f.type=scenario==3?0:65;put(0x8000,f);f.id=7;put(0x9000,f);put(0x80e0,Address(0x9000));
  osp::nodes::Object o{};o.name=0x5000;o.local.words[0]=o.local.words[5]=o.local.words[10]=o.local.words[15]=0x3f800000;
  o.local.words[3]=0x7f800001;o.local.words[12]=0x80000000;o.world=o.local;put(0x1000,o);put(0x2000,o);
  put(0x5000,osp::nodes::StringEntry{0,1,10,0});text(0x5018,"synthetic");put(0x4000,Address(0x4100));text(0x4100,"NiNode");
  if(scenario==4&&a==0x9000)return false;
  std::memcpy(p,b.data()+a-0x1000,n);return true;
 }
};
template<class T>std::optional<T>read(Reader&r,Address p){return osp::read_at<Reader,T>(r,p);}
struct NodeCalls{Reader&reader;std::optional<Address>get(Address self,std::size_t slot,Address arg=0){
 if(slot==osp::nodes::actor_root_slot||slot==osp::nodes::object_root_slot)return scenario==2?0:(arg?0x2000:0x1000);
 if(slot==2)return 0x4000;if(slot==3)return self;if(slot==8)return 0;return std::nullopt;
}};
bool osp_call_word(Address,Address,Address,std::uint64_t*out){*out=scenario==1?0:0x8000;return scenario!=5;}
struct LARGE_INTEGER{long long QuadPart;};
bool QueryPerformanceCounter(LARGE_INTEGER*p){p->QuadPart=12345;return scenario!=6;}
int InterlockedIncrement(int*p){return++*p;}
unsigned GetCurrentProcessId(){return 123;}
void log_line(const std::string&){}
void error(Json::Array&a,const std::string&s){a.emplace_back(s);}
void atomic_write(const std::wstring&,const std::string&s){std::cout<<s;}
struct State{Address image=0;std::wstring directory=L"synthetic";std::string session="SYNTHETIC";int node_sequence=0;LARGE_INTEGER node_clock_origin{10},node_clock_frequency{1000};}instance;
State*state=&instance;
'''
SUFFIX='\nint main(){for(scenario=0;scenario<7;++scenario){state->node_sequence=0;capture_nodes({"synthetic",20});}}\n'


def fixture(source,compiler):
    cpp=ROOT/'build/legacy-nodes-fixture.cpp';exe=cpp.with_suffix('')
    cpp.write_text(PREFIX+function(source,'capture_nodes')+SUFFIX)
    subprocess.run([compiler,'-std=c++17','-O2','-I'+str(ROOT/'include'),str(cpp),'-o',str(exe)],check=True)
    rows=subprocess.check_output([str(exe)]).splitlines(keepends=True)
    assert len(rows)==7
    for row in rows:json.loads(row)
    return [hashlib.sha256(row).hexdigest()for row in rows]


def main():
    p=json.loads((ROOT/'tests/legacy-v6-outputs.json').read_text());s=(ROOT/'src/plugin.cpp').read_text()
    assert hashlib.sha256(function(s,'capture_nodes').encode()).hexdigest()==p['capture_nodes_sha256']
    assert hashlib.sha256((ROOT/'include/osp/nodes.hpp').read_bytes()).hexdigest()==p['nodes_header_sha256']
    assert fixture(s,sys.argv[1]if len(sys.argv)>1 else'c++')==p['fixture_sha256']
    print('PASS: seven v6 node payloads byte-identical to plugin v6; actual serializer and shared traversal pinned')


if __name__=='__main__':main()
