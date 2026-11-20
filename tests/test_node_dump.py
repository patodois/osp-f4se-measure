# SPDX-License-Identifier: MIT OR Apache-2.0
"""Synthetic files only: no native frameworks, subprocesses or live game."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import node_dump as nd

def scalar(bits):
    n=struct.unpack('<f',struct.pack('<I',bits))[0]
    return dict(bits=f'{bits:08X}',decimal=n if n==n and abs(n)!=float('inf') else None)

def fixture():
    t=dict(rotation=[[scalar(0x3f800000 if i==j else 0)for j in range(3)]for i in range(3)],translation=[scalar(0x80000000),scalar(0),scalar(0)],scale=scalar(0x3f800000),rotation_padding=[scalar(0x7fc00001)]*3)
    n=dict(index=0,parent_index=None,depth=0,address='0000000000001000',name='synthetic',type='NiNode',kind='node',local=t,world=t,external_parent_world=None,skin_bones=[])
    tr=dict(complete=True,nodes=[n],stops=[],limits=dict(nodes=4096,depth=128,bones=2048,children=8192))
    return dict(node_dump_schema=1,plugin_version=6,requested_form_id='00000014',reference_form_id='00000014',base_form_id='00000007',dump_counter=1,monotonic_ticks=0,monotonic_frequency=10000000,complete=True,errors=[],roots=[dict(view=v,tree=copy.deepcopy(tr))for v in ('third_person','first_person')])

class NodeReader(unittest.TestCase):
    def test_exact_float_bits_and_nonfinite_padding(self):
        x=fixture();nd.validate(x)
        t=nd.transform(x['roots'][0]['tree']['nodes'][0]['local'])
        self.assertEqual(struct.pack('<f',t['translation'][0]),bytes.fromhex('00000080'))
        self.assertNotEqual(t['rotation_padding'][0],t['rotation_padding'][0])
    def test_invalid_decimal_hex_and_shape_rejected(self):
        for flaw in ('hex','decimal','shape','nonfinite'):
            x=fixture();t=x['roots'][0]['tree']['nodes'][0]['local']
            if flaw=='hex':t['scale']['bits']='0x3F800000'
            elif flaw=='decimal':t['scale']['decimal']=2
            elif flaw=='shape':t['rotation'][0].pop()
            else:t['rotation_padding'][0]['decimal']=0
            with self.assertRaises(ValueError):nd.validate(x)
    def test_indices_duplicate_addresses_and_player_roots(self):
        for flaw in ('index','parent','duplicate','roots','clock'):
            x=fixture();tr=x['roots'][0]['tree'];n=copy.deepcopy(tr['nodes'][0]);n.update(index=1,parent_index=0,depth=1,address='0000000000002000');tr['nodes'].append(n)
            if flaw=='index':n['index']=3
            elif flaw=='parent':n['parent_index']=1
            elif flaw=='duplicate':n['address']=tr['nodes'][0]['address']
            elif flaw=='roots':x['roots'].pop()
            else:x['monotonic_ticks']=-1
            with self.assertRaises(ValueError):nd.validate(x)
    def test_incomplete_stops_are_readable_but_never_complete(self):
        x=fixture();tr=x['roots'][0]['tree'];tr['complete']=False;tr['stops']=[dict(reason='depth_limit')];x['complete']=False;nd.validate(x)
        x['complete']=True
        with self.assertRaises(ValueError):nd.validate(x)
    def test_cpp_traversal_sparse_children_cycle_and_skin(self):
        p=Path(__file__).resolve().parents[1]/'build/nodes-synthetic.jsonl'
        valid,cycle=[json.loads(l)for l in p.read_text().splitlines()]
        nd.tree(valid);nd.tree(cycle)
        self.assertEqual([n['parent_index']for n in valid['nodes']],[None,0,0])
        self.assertEqual(valid['nodes'][1]['skin_bones'][0]['name'],'synthetic root')
        self.assertTrue(any(s['reason']=='cycle_or_shared_node'for s in cycle['stops']))
    def test_independent_coverage_scopes_and_missing_skin_names(self):
        x=fixture();tr=x['roots'][0]['tree'];n=tr['nodes'][0]
        tr.update(complete=False,transforms_and_hierarchy_complete=True,names_complete=True,types_complete=True,skins_complete=False,stops=[dict(reason='skin_bone_or_name_unreadable',scope='skin')])
        n.update(skin_complete=False,skin_missing_names=1,skin_bones=[dict(slot=0,address='0000000000000000',name=None)])
        x['complete']=False;nd.validate(x)
        for flaw in ('count','scope','overall','partial_scope','empty_complete'):
            y=copy.deepcopy(x);t=y['roots'][0]['tree']
            if flaw=='count':t['nodes'][0]['skin_missing_names']=0
            elif flaw=='scope':t['skins_complete']=True
            elif flaw=='overall':t['complete']=True
            elif flaw=='partial_scope':del t['names_complete']
            else:t.update(nodes=[],stops=[],complete=True,skins_complete=True)
            with self.assertRaises(ValueError):nd.validate(y)

if __name__=='__main__':unittest.main()
