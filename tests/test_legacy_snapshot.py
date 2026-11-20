# SPDX-License-Identifier: MIT OR Apache-2.0
"""Compile the actual legacy serializer with synthetic inputs and pin its bytes.

The golden digests were generated with the plugin v5 implementation, not v6.
No native framework, Wine or game process is called by this test.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    match=re.search(r'(?m)^[^\n;]*\b'+re.escape(name)+r'\([^;]*?\)\s*(?:noexcept\s*)?\{',source)
    if not match:raise ValueError(name)
    i=match.end();depth=1;state='code'
    while depth:
        c=source[i];n=source[i+1:i+2]
        if state=='code':
            if c in '\"\'':state=c
            elif c=='/'and n=='/':state='line';i+=1
            elif c=='/'and n=='*':state='block';i+=1
            elif c=='{':depth+=1
            elif c=='}':depth-=1
        elif state in ('\"',"'"):
            if c=='\\':i+=1
            elif c==state:state='code'
        elif state=='line'and c=='\n':state='code'
        elif state=='block'and c=='*'and n=='/':state='code';i+=1
        i+=1
    return source[match.start():i]

PREFIX=r'''
#include "osp/console.hpp"
#include <iostream>
#include <optional>
using osp::Json;using osp::layout::Address;
struct Reader {};
bool good=true;
struct State {Address image=0;int sequence=6;unsigned runtime=0x010a0a30,f4se=0x00060170,release=25;std::string session="SYNTHETIC";std::wstring directory=L"SYNTHETIC";bool console_registered=true;} value;
State* state=&value;
template<class T>std::optional<T> read(Reader&,Address){if(!good)return std::nullopt;return T{};}
void error(Json::Array& errors,const std::string& s){errors.emplace_back(s);}
void log_line(const std::string&){}
std::string utc_now(){return "2000-01-01T00:00:00.000Z";}
unsigned long long GetTickCount64(){return 12345;}
unsigned GetCurrentProcessId(){return 123;}
int InterlockedIncrement(int* p){return ++*p;}
void atomic_write(const std::wstring&,const std::string& s){std::cout<<s;}
Json mod_list(Reader&,const osp::layout::DataHandler&,Json::Array&){return Json::object({{"complete",true},{"items",Json::Array{Json("synthetic")}}});}
Json native_modules(Json::Array&){return Json::object({{"module_enumeration_complete",true},{"items",Json::Array{}}});}
Json player_snapshot(Reader&,Address,const osp::layout::DataHandler*,Json::Array&){return Json::object({{"complete",good},{"level",1},{"synthetic",Json::Array{Json(-0.0),Json("synthetic\n\"\\\xFF"),Json()}}});}
struct SettingsSnapshot {Json json;bool complete;};
SettingsSnapshot settings_snapshot(Reader&,Json::Array&){return {Json::object({{"complete",good},{"items",Json::Array{}}}),good};}
Json console_catalog(Reader&){return Json::Array{Json("synthetic"),Json("ospsnap")};}
'''
SUFFIX=r'''
int main(){for(bool g:{true,false})for(bool light:{false,true})for(auto why:{osp::Trigger::loaded,osp::Trigger::console,osp::Trigger::new_game}){good=g;value.sequence=6;capture(why,"synthetic-tag",light);}}
'''

def bytes_for(source,compiler,stem):
    cpp=ROOT/'build'/(stem+'.cpp');exe=cpp.with_suffix('')
    cpp.write_text(PREFIX+function(source,'capture')+SUFFIX)
    subprocess.run([compiler,'-std=c++17','-O2','-I'+str(ROOT/'include'),str(cpp),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],check=True,capture_output=True).stdout
    rows=result.splitlines(keepends=True)
    assert len(rows)==12
    for r in rows:json.loads(r)
    return [hashlib.sha256(r).hexdigest()for r in rows]

def catalogue_bytes_for(source,compiler,stem,alias):
    prefix=r'''
#include "osp/console.hpp"
#include <iostream>
#include <cstring>
#include <map>
using osp::Json;using osp::layout::Address;
void console_dispatch(){}
struct State{Address image=0;} instance;State* state=&instance;
struct Reader{
 std::map<Address,std::string> strings{{0x10000,"synthetic"},{0x20000,"s"},{0x30000,"ospsnap"},{0x40000,ALIAS}};
 bool read(Address a,void* out,std::size_t n){for(const auto& kv:strings)if(a>=kv.first&&a+n<=kv.first+kv.second.size()+1){std::memcpy(out,kv.second.c_str()+a-kv.first,n);return true;}return false;}
};
template<class T>std::optional<T>read(Reader&,Address p){
 if(p!=osp::layout::console_commands_rva&&p!=osp::layout::console_commands_rva+0x50)return std::nullopt;
 T c{};bool own=p==osp::layout::console_commands_rva+0x50;c.long_name=own?0x30000:0x10000;c.short_name=own?0x40000:0x20000;c.execute=own?reinterpret_cast<Address>(&console_dispatch):0;return c;
}
'''.replace('ALIAS','"ospnodes"'if alias else'""')
    cpp=ROOT/'build'/(stem+'.cpp');exe=cpp.with_suffix('')
    cpp.write_text(prefix+function(source,'console_catalog')+'\nint main(){Reader r;std::cout<<osp::encode(console_catalog(r));}\n')
    subprocess.run([compiler,'-std=c++17','-O2','-I'+str(ROOT/'include'),str(cpp),'-o',str(exe)],check=True)
    return subprocess.run([str(exe)],check=True,capture_output=True).stdout

def main():
    source=(ROOT/'src/plugin.cpp').read_text()
    expected=json.loads((ROOT/'tests/legacy-v5-sha256.json').read_text())
    for name,digest in expected.items():assert hashlib.sha256(function(source,name).encode()).hexdigest()==digest,name+' legacy body changed'
    pinned=json.loads((ROOT/'tests/legacy-v5-outputs.json').read_text())
    for name,digest in pinned['unchanged_files'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
    actual=bytes_for(source,sys.argv[1]if len(sys.argv)>1 else'c++','legacy-v6-fixture')
    assert actual==pinned['fixture_sha256'],'legacy snapshot output bytes changed'
    catalog=catalogue_bytes_for(source,sys.argv[1]if len(sys.argv)>1 else'c++','legacy-v6-catalogue',True)
    assert hashlib.sha256(catalog).hexdigest()==pinned['catalogue_sha256'],'new alias changed old catalogue bytes'
    print('PASS: 12 legacy v5 synthetic payloads and command catalogue byte-identical; seven legacy functions and shared readers/writer pinned')

if __name__=='__main__':main()
