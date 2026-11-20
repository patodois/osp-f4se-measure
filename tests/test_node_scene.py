# SPDX-License-Identifier: MIT OR Apache-2.0
import copy
import json
from pathlib import Path
import unittest
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import scene_dump as sd
import camera_math as cm
import node_math as nm
import bone_proofs as bp
from test_node_dump import fixture


class Scene(unittest.TestCase):
    def parts(self):
        return [json.loads(x)for x in (Path(__file__).resolve().parents[1]/'build/scene-synthetic.jsonl').read_text().splitlines()]
    def test_cpp_candidates_never_claim_verified(self):
        flat,cam=self.parts();sd.flattened(flat);sd.camera(cam)
        self.assertEqual(flat['bone_count'],2);self.assertIn('candidate',flat['validation_status'])
        self.assertEqual(flat['bones'][1]['parent_index'],0)
    def test_corrupt_parent_cycle_and_counts_rejected(self):
        for kind in ('cycle','count','name','shape','status'):
            flat,_=self.parts()
            if kind=='cycle':flat['bones'][0]['parent_index']=1
            elif kind=='count':flat['bone_count']=3
            elif kind=='name':flat['bones'][0]['name']=42
            elif kind=='shape':flat['bones'][0]['local']['rotation'].pop()
            else:flat['validation_status']='verified'
            with self.assertRaises(ValueError):sd.flattened(flat)
    def test_schema2_and_camera_fields(self):
        flat,cam=self.parts();d=fixture();d.update(node_dump_schema=2,plugin_version=7,camera=cam,flattened_layout_validation='requires_four_external_proofs')
        for r in d['roots']:r['flattened_bone_trees']=[copy.deepcopy(flat)]
        sd.validate(d)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'build')as directory:
            p=Path(directory)/'synthetic.json';p.write_text(json.dumps(d));self.assertEqual(sd.load(p)['plugin_version'],7)
            p.write_text('{"x": NaN}')
            with self.assertRaises(ValueError):sd.load(p)
        d['camera']['camera']['world_to_camera_raw'].pop()
        with self.assertRaises(ValueError):sd.validate(d)
    def test_projection_known_center_corner_and_viewport(self):
        c=dict(world=nm.identity(),frustum=[-1,1,1,-1,1,100],viewport=[0,1,1,0]);h=dict(family='pose_frustum',order='rows',axes=[0,1,2],signs=[1,1,1],frustum='slopes')
        self.assertEqual(cm.project([0,0,10],c,h,800,600),[400,300])
        self.assertEqual(cm.project([1,1,1],c,h,800,600),[800,0])
        c['viewport']=[0.25,0.75,0.75,0.25];self.assertEqual(cm.project([-1,-1,1],c,h,800,600),[200,450])
        with self.assertRaises(ValueError):cm.project([0,0,-1],c,h,800,600)
    def test_hypotheses_unique_and_matrix_perspective(self):
        hs=cm.hypotheses();self.assertEqual(len(hs),196);self.assertEqual(len({h['id']for h in hs}),196)
        c=dict(matrix=[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,1,0]],viewport=[0,1,1,0]);h=dict(family='raw4x4',order='rows',y_sign=1)
        self.assertEqual(cm.project([1,1,2],c,h,800,600),[600,150])
    def test_all_four_proofs_required_and_no_pose_on_failure(self):
        flat,_=self.parts();ordinary=dict(nodes=[]);forecast=dict(bones=[])
        for i,b in enumerate(flat['bones']):
            b['node_address']=f'{0x6000+i*0x1000:016X}'
            ordinary['nodes'].append(dict(name=b['name'],address=b['node_address'],local=copy.deepcopy(b['local']),world=copy.deepcopy(b['world'])))
            rest=copy.deepcopy(b['local']);rest.pop('rotation_padding');forecast['bones'].append(dict(name=b['name'],parent_name='root'if i else None,local=rest))
        r=bp.validate(flat,ordinary,forecast);self.assertTrue(r['accepted']);self.assertEqual(len(r['pose_comparison']),2)
        for flaw in ('count','parent','ordinary','composition'):
            x=copy.deepcopy(flat)
            if flaw=='count':x['bones'][0]['name']='unexpected'
            elif flaw=='parent':x['bones'][1]['parent_index']=-1
            elif flaw=='ordinary':x['bones'][1]['node_address']='0000000000009999'
            else:x['bones'][1]['world']['scale']=dict(bits='40000000',decimal=2)
            r=bp.validate(x,ordinary,forecast);self.assertFalse(r['accepted']);self.assertIsNone(r['pose_comparison'])
    def test_file_forecast_follows_ancestry_and_excludes_root_sibling(self):
        t=fixture()['roots'][0]['tree']['nodes'][0]['local']
        ns=[dict(index=i,parent_index=p,name=name,local=t)for i,p,name in [(0,None,'header'),(1,0,'root'),(2,1,'bone'),(3,0,'sibling')]]
        f=bp.file_forecast(dict(complete=True,nodes=ns),'root');self.assertEqual([n['name']for n in f['bones']],['bone']);self.assertEqual(f['bones'][0]['external_parent_name'],'root');self.assertEqual([n['name']for n in f['outside_flattened_array']],['header','root','sibling'])
        scoped=dict(complete=False,nodes=ns,stops=[dict(reason='decoded AV transform unavailable',non_bone_type='NiCamera')]);self.assertEqual(bp.file_forecast(scoped,'root')['bone_count'],1)
        scoped['stops'][0]['non_bone_type']='unknown'
        with self.assertRaises(ValueError):bp.file_forecast(scoped,'root')
        ns[2]['parent_index']=3
        with self.assertRaises(ValueError):bp.file_forecast(dict(complete=True,nodes=ns),'root')


if __name__=='__main__':unittest.main()
