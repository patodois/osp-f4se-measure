# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pin the actual schema-2 v7 serializer before adding schema 3."""
from pathlib import Path
import hashlib,json,subprocess,sys
from test_legacy_snapshot import function
from test_legacy_nodes import PREFIX
ROOT=Path(__file__).resolve().parents[1]
PREFIX=PREFIX.replace('#include "osp/nodes.hpp"','#include "osp/scene.hpp"').replace('int node_sequence=0;','int node_sequence=0,scene_sequence=0;')
PREFIX+='\nJson window_snapshot(){return Json::object({{"complete",false}});}\n'
SUFFIX='\nint main(){for(scenario=0;scenario<7;++scenario){state->scene_sequence=0;capture_scene({"synthetic",20});}}\n'
def fixture(source,compiler):
 cpp=ROOT/'build/legacy-scene-fixture.cpp';exe=cpp.with_suffix('');cpp.write_text(PREFIX+function(source,'capture_scene')+SUFFIX)
 subprocess.run([compiler,'-std=c++17','-O2','-I'+str(ROOT/'include'),str(cpp),'-o',str(exe)],check=True)
 rows=subprocess.check_output([str(exe)]).splitlines(keepends=True);assert len(rows)==7
 for row in rows:json.loads(row)
 return [hashlib.sha256(row).hexdigest()for row in rows]
def main():
 p=json.loads((ROOT/'tests/legacy-v7-outputs.json').read_text());s=(ROOT/'src/plugin.cpp').read_text()
 for name in('capture_scene','window_snapshot'):assert hashlib.sha256(function(s,name).encode()).hexdigest()==p[name+'_sha256']
 assert hashlib.sha256((ROOT/'include/osp/scene.hpp').read_bytes()).hexdigest()==p['scene_header_sha256']
 assert fixture(s,sys.argv[1]if len(sys.argv)>1 else'c++')==p['fixture_sha256']
 print('PASS: seven v7 schema-2 payloads byte-identical to plugin v7; actual serializer/camera/window paths pinned')
if __name__=='__main__':main()
