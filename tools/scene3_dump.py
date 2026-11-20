# SPDX-License-Identifier: MIT OR Apache-2.0
"""Schema-3 reader and independent four-proof recomputation. No game access."""
import copy,json,math,struct
from pathlib import Path
import scene_dump as previous
import node_dump as nd
import node_math as nm
import bone_proofs as old

def all_words(t):return [v['bits']for row,pad in zip(t['rotation'],t['rotation_padding'])for v in [*row,pad]]+[v['bits']for v in t['translation']]+[t['scale']['bits']]

def compose(parent,local,ftz):
    def rounder(v):
        r=nm.f32(v);bits=nm.bits(r)
        return math.copysign(0.,r)if ftz and 0<(bits&0x7fffffff)<0x800000 else r
    def mul(a,b):return rounder(a*b)
    def add(a,b):return rounder(a+b)
    def dot(a,b):return add(add(mul(a[0],b[0]),mul(a[1],b[1])),mul(a[2],b[2]))
    p,l=parent['rotation'],local['rotation']
    return dict(rotation=[[dot(l[i],[p[k][j]for k in range(3)])for j in range(3)]for i in range(3)],translation=[add(parent['translation'][i],mul(parent['scale'],dot([p[k][i]for k in range(3)],local['translation'])))for i in range(3)],scale=mul(parent['scale'],local['scale']))

def validate(d):
    if d.get('node_dump_schema')!=3 or d.get('plugin_version')!=8:raise ValueError('expected schema3/plugin8')
    status=d.get('flattened_layout_validation')
    if status not in ('candidate_round19','validated_round19'):raise ValueError('invalid round19 validation state')
    compatible=copy.deepcopy(d);compatible.update(node_dump_schema=2,plugin_version=7,flattened_layout_validation='requires_four_external_proofs')
    for root in compatible['roots']:
        for flat in root['flattened_bone_trees']:flat['validation_status']='candidate_requires_four_external_proofs'
    previous.validate(compatible)
    gate=d.get('round19_validation',{})
    pauses=gate.get('accepted_player_pauses')
    if not isinstance(pauses,list) or len(pauses)>2 or any(type(p)is not int or p<=0 for p in pauses) or sorted(set(pauses))!=pauses:raise ValueError('invalid distinct-pause gate')
    if status=='validated_round19' and (len(pauses)!=2 or not gate.get('current_proofs_passed')):raise ValueError('contradictory validated layout')
    for root in d['roots']:
        for flat in root['flattened_bone_trees']:
            if flat.get('validation_status')!=status:raise ValueError('tree/header validation differs')
            if not isinstance(flat.get('round19_proofs'),dict):raise ValueError('missing four proofs')
            for b in flat['bones']:
                for k in ('effective_local','effective_world'):
                    if flat['round19_proofs'].get('complete') or k in b:nd.transform(b[k])
                for k in ('node_local','node_world'):
                    if b.get(k)is not None:nd.transform(b[k])
    return d

def load(path):
    def reject(x):raise ValueError('nonstandard JSON '+x)
    return validate(json.loads(Path(path).read_text(),parse_constant=reject))

def four_proofs(flat,ordinary,forecast):
    if flat.get('round19_proofs',{}).get('complete')is not True:
        return dict(proofs={k:False for k in ('names_and_order','parent_indices','common_pointer_name_and_world','composition_ftz','composition_without_ftz')},accepted=False,case_probes=[],bones=[],reason='incomplete_native_field_evidence')
    bs,expected=flat['bones'],forecast['bones'];nodes={n['address']:n for n in ordinary['nodes']}
    expected_indices={b['name'].lower():i for i,b in enumerate(expected)}
    names=(flat.get('round19_proofs',{}).get('complete')is True) and flat['bone_count']==len(bs) and 0<=flat['bone_count_expanded']<=2048 and ordinary['transforms_and_hierarchy_complete'] and len(bs)==len(expected) and len(expected_indices)==len(expected)
    parents=names;common=names;rows=[];case=[];strict=names;ftz=names
    roots=[n for n in ordinary['nodes']if n['address']==flat['address']]
    root=roots[0]if len(roots)==1 else None
    names=names and root is not None and root['name'].lower()==forecast['root_name'].lower()
    effective=[];worlds=[]
    for i,b in enumerate(bs):
        e=expected[i]if i<len(expected)else {};name=b.get('name')
        ne=bool(isinstance(name,str) and name.lower()==e.get('name','').lower())
        if name!=e.get('name'):
            p=b.get('case_probe')or{};passed=p.get('constructed')is True and p.get('released')is True and p.get('file_spelling')==e.get('name') and p.get('runtime_spelling')==name and p.get('constructed_entry')==p.get('record_entry')==b.get('name_entry_address') and b.get('name_entry_address')!='0000000000000000'
            case.append(dict(index=i,name=name,file=e.get('name'),pointer=p.get('constructed_entry'),passed=passed));ne=ne and passed
        names=names and ne
        want=-1 if e.get('parent_name')is None else expected_indices.get(e['parent_name'].lower(),-2)
        pe=b['parent_index']==want;parents=parents and pe
        node=None if b['node_address']=='0000000000000000'else nodes.get(b['node_address'])
        ce=b['node_address']=='0000000000000000' or (node is not None and node['name']==name and b.get('node_name_entry_address')==b.get('name_entry_address') and all_words(b['world'])==all_words(node['world']) and all_words(b['node_world'])==all_words(node['world']) and all_words(b['node_local'])==all_words(node['local']))
        common=common and ce
        local=node['local']if node else b['local'];world=node['world']if node else b['world'];effective.append(local);worlds.append(world)
        if all_words(local)!=all_words(b['effective_local']) or all_words(world)!=all_words(b['effective_world']):common=False
        rows.append(dict(index=i,name=name,names=ne,parent=pe,common=ce,has_node=node is not None))
    for i,b in enumerate(bs):
        p=b['parent_index'];valid=root is not None and -1<=p<i
        equal=[]
        for flush in (False,True):
            prediction=compose(old.semantic(root['world']if p<0 else worlds[p]),old.semantic(effective[i]),flush)if valid else None
            equal.append(bool(prediction and [f'{nm.bits(v):08X}'for v in nm.components(prediction)]==old.words(worlds[i])))
        rows[i].update(without_ftz=equal[0],with_ftz=equal[1]);strict=strict and equal[0];ftz=ftz and equal[1]
    proofs=dict(names_and_order=names,parent_indices=parents,common_pointer_name_and_world=common,composition_ftz=ftz,composition_without_ftz=strict)
    return dict(proofs=proofs,accepted=all(proofs[k]for k in ('names_and_order','parent_indices','common_pointer_name_and_world','composition_ftz')),case_probes=case,bones=rows)

def pose_metrics(flat,forecast,proof,validated):
    if not validated or not proof['accepted']:raise ValueError('pose comparison requires independent prospective validation')
    rows=[]
    for b,e in zip(flat['bones'],forecast['bones']):
        a,z=old.semantic(b['effective_local']),old.semantic(e['local']);record=old.semantic(b['local'])
        def distance(x,y):return math.sqrt(sum((a-b)**2 for a,b in zip(x,y)))
        def angle(x,y):
            try:return nm.rotation_angle(x,y)
            except ValueError:return None
        rows.append(dict(index=b['index'],name=b['name'],source=b['effective_local_source'],effective_file_angle_radians=angle(a['rotation'],z['rotation']),effective_file_translation=distance(a['translation'],z['translation']),record_file_angle_radians=angle(record['rotation'],z['rotation']),record_file_translation=distance(record['translation'],z['translation']),record_equals_file=old.words(b['local'])==old.words(e['local']),record_equals_effective=old.words(b['local'])==old.words(b['effective_local'])))
    return rows
