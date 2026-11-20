# SPDX-License-Identifier: MIT OR Apache-2.0
"""Strict reader for osp_measure node_dump_schema 1; no game/process access."""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def scalar(value):
    if not isinstance(value, dict) or not re.fullmatch(r'[0-9A-F]{8}', value.get('bits', '')):
        raise ValueError('float requires exactly eight uppercase hexadecimal bits')
    bits = int(value['bits'], 16)
    number = struct.unpack('<f', struct.pack('<I', bits))[0]
    decimal = value.get('decimal')
    if math.isfinite(number):
        if type(decimal) not in (float, int) or decimal != number:
            raise ValueError('decimal disagrees with float bits')
        # JSON may spell -0 as integer 0. Bits remain authoritative.
    elif decimal is not None:
        raise ValueError('nonfinite float decimal must be null')
    return number


def transform(value):
    if not isinstance(value, dict):
        raise ValueError('missing transform')
    rotation, translation = value.get('rotation'), value.get('translation')
    padding = value.get('rotation_padding')
    if not isinstance(rotation, list) or len(rotation) != 3 or any(not isinstance(r, list) or len(r) != 3 for r in rotation):
        raise ValueError('rotation must be 3 by 3')
    if not isinstance(translation, list) or len(translation) != 3 or not isinstance(padding, list) or len(padding) != 3:
        raise ValueError('translation and rotation padding require three scalars')
    return dict(rotation=[[scalar(v) for v in r] for r in rotation],
                translation=[scalar(v) for v in translation], scale=scalar(value['scale']),
                rotation_padding=[scalar(v) for v in padding])


def tree(value):
    if not isinstance(value, dict) or type(value.get('complete')) is not bool:
        raise ValueError('invalid tree envelope')
    nodes, stops = value.get('nodes'), value.get('stops')
    limits = value.get('limits', {})
    if not isinstance(nodes, list) or not isinstance(stops, list) or len(nodes) > limits.get('nodes', 0):
        raise ValueError('invalid node count or missing limits')
    if value['complete'] and stops:
        raise ValueError('complete tree has stops')
    if value['complete'] and not nodes:
        raise ValueError('complete tree is empty')
    scopes = ('transforms_and_hierarchy_complete', 'names_complete', 'types_complete', 'skins_complete')
    if any(name in value for name in scopes):
        if any(type(value.get(name)) is not bool for name in scopes):
            raise ValueError('coverage scopes must all be boolean')
        if value['complete'] and not all(value[name] for name in scopes):
            raise ValueError('complete tree has incomplete scope')
        scope_names = dict(hierarchy=scopes[0], name=scopes[1], type=scopes[2], skin=scopes[3])
        for stop in stops:
            if isinstance(stop, dict):
                name = scope_names.get(stop.get('scope', 'hierarchy'))
                if name and value[name]:
                    raise ValueError('stop contradicts complete scope')
    addresses, stack = set(), []
    for i, node in enumerate(nodes):
        if node.get('index') != i:
            raise ValueError('noncontiguous DFS index')
        parent = node.get('parent_index')
        depth = node.get('depth')
        if i == 0:
            if parent is not None or depth != 0:
                raise ValueError('root parent/depth')
        elif type(parent) is not int or not 0 <= parent < i or depth != nodes[parent]['depth']+1:
            raise ValueError('invalid parent/depth')
        if type(depth) is not int or not 0 <= depth <= limits.get('depth', -1):
            raise ValueError('depth outside bound')
        while len(stack) > depth:
            stack.pop()
        if i and (not stack or stack[-1] != parent):
            raise ValueError('nodes are not in depth-first traversal order')
        stack.append(i)
        address = node.get('address')
        if not isinstance(address, str) or not re.fullmatch(r'[0-9A-F]{16}', address) or address in addresses:
            raise ValueError('duplicate/invalid node address')
        addresses.add(address)
        for field in ('name', 'type'):
            if node.get(field) is not None and not isinstance(node[field], str):
                raise ValueError('invalid name/type')
        if node.get('kind') not in ('node', 'leaf', 'unknown'):
            raise ValueError('invalid node kind')
        for field in ('local', 'world'):
            transform(node[field])
        if node.get('external_parent_world') is not None:
            transform(node['external_parent_world'])
        bones = node.get('skin_bones')
        if not isinstance(bones, list) or len(bones) > limits.get('bones', 0):
            raise ValueError('invalid bone list')
        for slot, bone in enumerate(bones):
            if bone.get('slot') != slot or bone.get('name') is not None and not isinstance(bone['name'], str):
                raise ValueError('invalid bone slot/name')
        if 'skin_missing_names' in node:
            missing = node['skin_missing_names']
            if type(missing) is not int or missing != sum(b['name'] is None for b in bones):
                raise ValueError('skin missing-name count disagrees with slots')
            if missing and node.get('skin_complete') is not False:
                raise ValueError('skin with missing names claims complete')
    for stop in stops:
        if not isinstance(stop, dict) or not isinstance(stop.get('reason'), str):
            raise ValueError('invalid stop reason')
    return value


def validate(value):
    if not isinstance(value, dict) or value.get('node_dump_schema') != 1 or value.get('plugin_version') != 6:
        raise ValueError('unsupported node dump schema/plugin')
    for name in ('requested_form_id', 'reference_form_id', 'base_form_id'):
        v = value.get(name)
        if v is None and name != 'requested_form_id':
            continue
        if not isinstance(v, str) or not re.fullmatch(r'[0-9A-F]{8}', v):
            raise ValueError('invalid '+name)
    for name in ('dump_counter', 'monotonic_frequency'):
        if type(value.get(name)) is not int or value[name] <= 0:
            raise ValueError('invalid '+name)
    if type(value.get('monotonic_ticks')) is not int or value['monotonic_ticks'] < 0:
        raise ValueError('invalid monotonic clock')
    if not isinstance(value.get('roots'), list) or not isinstance(value.get('errors'), list) or type(value.get('complete')) is not bool:
        raise ValueError('invalid dump completeness/envelope')
    views = []
    for root in value['roots']:
        if root.get('view') not in ('third_person', 'first_person', 'object') or root['view'] in views:
            raise ValueError('invalid/duplicate root view')
        views.append(root['view'])
        tree(root.get('tree'))
    if value['complete'] and (value['errors'] or not views or any(not r['tree']['complete'] for r in value['roots'])):
        raise ValueError('dump claims complete despite missing/error tree')
    if value['complete'] and value['requested_form_id'] == '00000014' and views != ['third_person', 'first_person']:
        raise ValueError('complete player dump requires both labelled roots')
    return value


def load(path):
    def reject_number(text):
        raise ValueError('nonstandard JSON number '+text)
    return validate(json.loads(Path(path).read_text(encoding='utf-8'), parse_constant=reject_number))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    args = parser.parse_args()
    value = load(args.dump)
    print(json.dumps(dict(complete=value['complete'], dump_counter=value['dump_counter'],
                         roots=[dict(view=r['view'], nodes=len(r['tree']['nodes']), stops=r['tree']['stops']) for r in value['roots']]), indent=2))


if __name__ == '__main__':
    main()
