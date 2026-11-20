# SPDX-License-Identifier: MIT OR Apache-2.0
"""Strict schema-2 reader. Readable candidates are never automatically verified."""
import json
from pathlib import Path
import node_dump as nd


def flattened(value):
    if not isinstance(value,dict) or type(value.get('complete'))is not bool:raise ValueError('invalid flattened header')
    if value.get('validation_status')!='candidate_requires_four_external_proofs':raise ValueError('unexpected proof status')
    bones=value.get('bones');errors=value.get('errors')
    if not isinstance(bones,list)or not isinstance(errors,list)or len(bones)>2048:raise ValueError('invalid bone rows')
    count=value.get('bone_count');expanded=value.get('bone_count_expanded')
    if value['complete']and(type(count)is not int or type(expanded)is not int or not (0<=count<=2048 and 0<=expanded<=2048) or len(bones)!=count or errors):raise ValueError('contradictory complete bones')
    for i,b in enumerate(bones):
        if b.get('index')!=i or (b.get('name')is not None and not isinstance(b['name'],str)):raise ValueError('invalid bone identity')
        for field in ('parent_index','child_index','sibling_index'):
            index=b.get(field)
            if type(index)is not int or (value['complete']and not -1<=index<len(bones)):raise ValueError('invalid bone link')
        if type(b.get('child_count'))is not int or type(b.get('deferred_attach_raw'))is not int:raise ValueError('invalid bone fields')
        nd.transform(b['local']);nd.transform(b['world'])
    if value['complete']:
        for i in range(len(bones)):
            seen=set();at=i
            while at!=-1:
                if at in seen:raise ValueError('cyclic flattened parents')
                seen.add(at);at=bones[at]['parent_index']
    return value


def camera(value):
    if type(value.get('complete'))is not bool or not isinstance(value.get('errors'),list):raise ValueError('invalid camera header')
    nd.tree(value['root_tree'])
    if value['complete']and(value['errors']or value.get('camera')is None):raise ValueError('contradictory camera coverage')
    for key in ('world_fov_raw','first_person_fov_raw'):
        if value.get(key)is not None:nd.scalar(value[key])
    c=value.get('camera')
    if c is not None:
        nd.transform(c['world'])
        for key,length in [('world_to_camera_raw',16),('frustum_left_right_top_bottom_near_far',6),('viewport_left_right_top_bottom',4)]:
            if not isinstance(c.get(key),list)or len(c[key])!=length:raise ValueError('invalid camera shape')
            for x in c[key]:nd.scalar(x)
        if type(c.get('orthographic'))is not bool:raise ValueError('invalid projection kind')
    return value


def validate(value):
    if value.get('node_dump_schema')!=2 or value.get('plugin_version')!=7:raise ValueError('expected schema 2, plugin 7')
    # The inherited tree/snapshot fields use precisely the schema-1 contracts.
    base=dict(value,node_dump_schema=1,plugin_version=6)
    base['complete']=False
    nd.validate(base)
    for r in value['roots']:
        if not isinstance(r.get('flattened_bone_trees'),list):raise ValueError('missing flattened trees')
        for f in r['flattened_bone_trees']:flattened(f)
    camera(value['camera'])
    if value.get('flattened_layout_validation')!='requires_four_external_proofs':raise ValueError('unverified layout status must remain explicit')
    if value['complete']and(value['errors']or not value['camera']['complete']or any(not r['tree']['complete']or any(not f['complete']for f in r['flattened_bone_trees'])for r in value['roots'])):raise ValueError('contradictory scene completeness')
    return value


def load(path):
    def reject(value):raise ValueError('nonstandard JSON number: '+value)
    return validate(json.loads(Path(path).read_text(),parse_constant=reject))


if __name__=='__main__':
    import sys
    d=load(sys.argv[1]);print(json.dumps(dict(schema=2,complete=d['complete'],roots=len(d['roots']),candidate_bone_counts=[[f['bone_count']for f in r['flattened_bone_trees']]for r in d['roots']],camera_complete=d['camera']['complete'],layout_verified=False)))
