# SPDX-License-Identifier: MIT OR Apache-2.0
import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from node_math import identity,compose,rotation_angle,ulp,f32

class TransformHypotheses(unittest.TestCase):
    def hypothesis(self,**overrides):
        return dict(rotation_product='parent_local',translation_matrix='parent',translation_scale='parent',world_scale='product',**overrides)
    def test_parent_rotation_and_scale_are_independent_stimuli(self):
        p=identity();p.update(rotation=[[0.,-1.,0.],[1.,0.,0.],[0.,0.,1.]],translation=[10.,20.,30.],scale=2.)
        l=identity();l.update(translation=[1.,2.,3.],scale=.5)
        h=self.hypothesis();w=compose(p,l,h,'f32_after');self.assertEqual(w['translation'],[6.,22.,36.]);self.assertEqual(w['scale'],1.)
        h['translation_scale']='none';self.assertEqual(compose(p,l,h)['translation'],[8.,21.,33.])
        h['translation_matrix']='parent_transpose';self.assertEqual(compose(p,l,h)['translation'],[12.,19.,33.])
    def test_noncommuting_rotations_separate_products(self):
        p=identity();p['rotation']=[[0.,-1.,0.],[1.,0.,0.],[0.,0.,1.]]
        l=identity();l['rotation']=[[1.,0.,0.],[0.,0.,-1.],[0.,1.,0.]]
        h=self.hypothesis();a=compose(p,l,h);h['rotation_product']='local_parent';b=compose(p,l,h)
        self.assertNotEqual(a['rotation'],b['rotation'])
    def test_angle_is_zero_for_identical_and_correct_at_pi_and_tiny(self):
        i=identity()['rotation'];self.assertEqual(rotation_angle(i,i),0)
        self.assertAlmostEqual(rotation_angle(i,[[-1.,0.,0.],[0.,-1.,0.],[0.,0.,1.]]),math.pi)
        angle=1e-7;c,s=math.cos(angle),math.sin(angle)
        self.assertAlmostEqual(rotation_angle(i,[[c,-s,0.],[s,c,0.],[0.,0.,1.]]),angle,places=13)
    def test_invalid_rotation_is_not_a_pose_measurement(self):
        i=identity()['rotation']
        for bad in ([[2.,0.,0.],[0.,1.,0.],[0.,0.,1.]],[[-1.,0.,0.],[0.,1.,0.],[0.,0.,1.]]):
            with self.assertRaises(ValueError):rotation_angle(i,bad)
    def test_ulp_and_rounding_are_separate_from_bit_identity(self):
        self.assertEqual(ulp(0.,-0.),0);self.assertEqual(ulp(1.,1.+2**-23),1);self.assertEqual(f32(1.+2**-25),1.)
        self.assertIsNone(ulp(float('nan'),1.))

if __name__=='__main__':unittest.main()
