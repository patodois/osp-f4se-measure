# SPDX-License-Identifier: MIT OR Apache-2.0
import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import numpy as np
import native_photo as photo

class NativePhoto(unittest.TestCase):
    """Fixed 2x2 nearest-neighbour sampling and the encoded RGB8 difference."""
    def test_sampling(self):
        native=np.zeros((720,1280,3),dtype=np.uint8);native[200,300]=[1,2,3]
        physical=np.repeat(np.repeat(native,2,0),2,1)
        out,mask,r=photo.extract_array(physical,[0,0,2560,1440]);self.assertTrue(np.array_equal(out,native));self.assertEqual(r['nonuniform_blocks'],0)
        physical[1,1,0]=99;physical[401,601,1]=100
        out,mask,r=photo.extract_array(physical,[0,0,2560,1440]);self.assertEqual(r['nonuniform_blocks_xy'],[[0,0],[300,200]]);self.assertEqual(r['nonuniform_unmasked'],1);self.assertEqual(int(mask.sum()),1280*720-4*32*32);self.assertTrue(np.array_equal(out,native))
        self.assertEqual(photo.difference(out,native)['equal_fraction'],1)
        out[200,300,0]+=7;self.assertEqual(photo.difference(out,native)['max'],7)
        with self.assertRaises(ValueError):photo.extract_array(physical,[1,0,2560,1440])

# The actual C++ serializer's output is used as the independent reader fixture.
class ProofReader(unittest.TestCase):
    def fixture(self):
        import json,scene3_dump as s
        f=json.loads((Path(__file__).resolve().parents[1]/'build/scene3-synthetic.jsonl').read_text())
        root=copy.deepcopy(f['bones'][0]['world']);root['translation'][0]=dict(bits='41200000',decimal=10)
        ordinary=dict(transforms_and_hierarchy_complete=True,nodes=[dict(address=f['address'],name='synthetic-root',world=root),dict(address=f['bones'][0]['node_address'],name='Spine',local=f['bones'][0]['node_local'],world=f['bones'][0]['node_world'])])
        forecast=dict(root_name='synthetic-root',bones=[dict(name='SPINE',parent_name=None,local=f['bones'][0]['local']),dict(name='child',parent_name='SPINE',local=f['bones'][1]['local'])])
        return s,f,ordinary,forecast
    def test_incomplete_extra_fields_are_not_invented(self):
        import json,scene3_dump as reader
        from test_node_dump import fixture
        s,f,o,e=self.fixture();f['complete']=False;f['errors']=['legacy raw-byte diagnostic'];f['validation_status']='candidate_round19';f['round19_proofs']['complete']=False
        for b in f['bones']:
            b.pop('effective_local');b.pop('effective_world')
        self.assertFalse(s.four_proofs(f,o,e)['accepted'])
        d=fixture();cam=json.loads((Path(__file__).resolve().parents[1]/'build/scene-synthetic.jsonl').read_text().splitlines()[1]);d.update(node_dump_schema=3,plugin_version=8,complete=False,camera=cam,flattened_layout_validation='candidate_round19',round19_validation=dict(accepted_player_pauses=[],current_proofs_passed=False))
        for r in d['roots']:r['flattened_bone_trees']=[copy.deepcopy(f)]
        reader.validate(d)
    def test_empty_bone_is_not_external_parent(self):
        s,f,o,e=self.fixture();f['bones'][1]['name']='';e['bones'][1]['name']=''
        self.assertTrue(s.four_proofs(f,o,e)['accepted'])
    def test_independent_four_proofs_and_gate(self):
        s,f,o,e=self.fixture();p=s.four_proofs(f,o,e);self.assertTrue(p['accepted']);self.assertTrue(p['proofs']['composition_without_ftz']);self.assertEqual(p['case_probes'][0]['pointer'],f['bones'][0]['name_entry_address'])
        with self.assertRaises(ValueError):s.pose_metrics(f,e,p,False)
        self.assertEqual(len(s.pose_metrics(f,e,p,True)),2)
        for flaw in ('pointer','parent','padding','order','effective'):
            x=copy.deepcopy(f)
            if flaw=='pointer':x['bones'][0]['case_probe']['constructed_entry']='0000000000008888'
            elif flaw=='parent':x['bones'][1]['parent_index']=-1
            elif flaw=='padding':x['bones'][0]['world']['rotation_padding'][0]=dict(bits='00000001',decimal=2**-149)
            elif flaw=='order':x['bones'].reverse()
            else:x['bones'][0]['effective_local']['scale']=dict(bits='40000000',decimal=2)
            self.assertFalse(s.four_proofs(x,o,e)['accepted'],flaw)


if __name__=='__main__':unittest.main()
