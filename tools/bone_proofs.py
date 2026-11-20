# SPDX-License-Identifier: MIT OR Apache-2.0
"""Four mandatory, independent prospective flattened-layout proofs."""
import collections
import math
import node_dump as nd
import node_math as nm

H=dict(rotation_product='local_parent',translation_matrix='parent_transpose',translation_scale='parent',world_scale='product')


def words(t):
    return [v['bits']for row in t['rotation']for v in row]+[v['bits']for v in t['translation']]+[t['scale']['bits']]


def semantic(t):
    return dict(rotation=[[nd.scalar(v)for v in row]for row in t['rotation']],translation=[nd.scalar(v)for v in t['translation']],scale=nd.scalar(t['scale']))


def file_forecast(scene,root_name):
    ns=scene['nodes'];roots=[n['index']for n in ns if n['name']==root_name]
    stops=scene.get('stops',[])
    # A named NiCamera leaf is not a bone/NiNode. Its undecoded serialized
    # transform must remain disclosed, never be guessed or hide an unknown node.
    known_nonbone=bool(stops)and all(s.get('reason')=='decoded AV transform unavailable'and s.get('non_bone_type')=='NiCamera'for s in stops)
    if (not scene['complete']and not known_nonbone)or len(roots)!=1:raise ValueError('one complete named bone-file scope required')
    root=roots[0];inside=set()
    for i,n in enumerate(ns):
        if n['index']!=i or (n['parent_index']is not None and not 0<=n['parent_index']<i):raise ValueError('file parent order invalid')
        at=n['parent_index']
        while at is not None:
            if at==root:inside.add(i);break
            at=ns[at]['parent_index']
    bones=[];outside=[]
    for n in ns:
        p=n['parent_index'];parent=ns[p]['name']if p is not None else None
        row=dict(name=n['name'],parent_name=parent,local=n['local'],file_block=n.get('block'))
        if n['index']in inside:
            row.update(parent_name=parent if p in inside else None,external_parent_name=parent if p not in inside else None);bones.append(row)
        else:outside.append(row)
    return dict(file_nodes=len(ns),root_name=root_name,bone_count=len(bones),bones=bones,outside_flattened_array=outside,excluded_non_bone_blocks=stops)


def validate(flat,ordinary,forecast):
    bs=flat['bones'];expected=forecast['bones'];names=[b['name']for b in bs];en=[b['name']for b in expected]
    unique=len(set(names))==len(names)and len(set(en))==len(en)
    quantity_names=flat['complete']and unique and len(bs)==len(expected)and set(names)==set(en)
    em={b['name']:b for b in expected};parent_rows=[]
    for b in bs:
        p=b['parent_index'];actual=bs[p]['name']if 0<=p<len(bs)else None
        want=em.get(b['name'],{}).get('parent_name');parent_rows.append(dict(name=b['name'],expected=want,measured=actual,equal=b['name']in em and want==actual))
    parents=quantity_names and all(r['equal']for r in parent_rows)
    ons=collections.defaultdict(list)
    for n in ordinary['nodes']:ons[n['name']].append(n)
    common=[]
    for b in bs:
        candidates=ons[b['name']]
        if candidates:
            n=candidates[0];eq=len(candidates)==1 and b['node_address']==n['address']and all(words(b[k])==words(n[k])for k in ('local','world'))
            common.append(dict(name=b['name'],ordinary_matches=len(candidates),node_pointer_equal=b['node_address']==n['address'],equal=eq,local_unequal=sum(x!=y for x,y in zip(words(b['local']),words(n['local']))),world_unequal=sum(x!=y for x,y in zip(words(b['world']),words(n['world']))),padding_equal=all(b[k]['rotation_padding']==n[k]['rotation_padding']for k in ('local','world'))))
    expected_common={n['name']for n in ordinary['nodes']if n['name']in em}
    ordinary_equal=bool(expected_common)and {r['name']for r in common}==expected_common and all(r['equal']for r in common)
    composition=[]
    for b in bs:
        p=b['parent_index']
        external_name=em.get(b['name'],{}).get('external_parent_name')
        if p<0:
            if not external_name:continue  # no invented world context
            candidates=ons[external_name]
            if len(candidates)!=1:composition.append(dict(index=b['index'],equal=False,reason='published ordinary external parent unavailable'));continue
            parent=semantic(candidates[0]['world']);origin='ordinary_external_parent'
        else:
            if p>=len(bs):composition.append(dict(index=b['index'],equal=False,reason='parent_outside_array'));continue
            parent=semantic(bs[p]['world']);origin='flattened_parent'
        prediction=nm.compose(parent,semantic(b['local']),H,'f32_after');target=semantic(b['world'])
        aa,bb=nm.components(prediction),nm.components(target);finite=all(math.isfinite(v)for v in aa+bb)
        composition.append(dict(index=b['index'],name=b['name'],parent_origin=origin,equal=finite and [f'{nm.bits(v):08X}'for v in aa]==words(b['world']),max_abs=max((abs(a-c)for a,c in zip(aa,bb)),default=None)if finite else None))
    composition_equal=bool(composition)and all(r['equal']for r in composition)
    proofs=dict(quantity_and_names=quantity_names,parent_indices=parents,ordinary_node_equality=ordinary_equal,composition=composition_equal)
    accepted=all(proofs.values());pose=None
    if accepted:
        pose=[]
        for b in bs:
            file=em[b['name']]['local'];measured=semantic(b['local']);source=semantic(file)
            try:angle=nm.rotation_angle(source['rotation'],measured['rotation'])
            except ValueError:angle=None
            pose.append(dict(name=b['name'],rest_bit_identical=words(file)==words(b['local']),translation_distance=math.dist(source['translation'],measured['translation']),rotation_radians=angle,scale_delta=measured['scale']-source['scale']))
    outside=[dict(name=n['name'],ordinary_matches=len(ons[n['name']]))for n in forecast.get('outside_flattened_array',[])]
    coverage=dict(file_nodes=forecast.get('file_nodes'),flattened_verified_names=len(names),ordinary_outside_present=sum(n['ordinary_matches']==1 for n in outside),outside=outside)if accepted else None
    return dict(accepted=accepted,proofs=proofs,expected_count=len(expected),measured_count=len(bs),missing_names=sorted(set(en)-set(names)),extra_names=sorted(set(names)-set(en)),parents=parent_rows,common_nodes=common,ordinary_expected_common=len(expected_common),composition_edges=composition,root_composition='published named ordinary external parent when the frozen file forecast identifies one; otherwise unasserted',pose_comparison=pose,combined_file_coverage=coverage,semantic_components=13,padding='preserved and compared separately; not rotation coefficients')
