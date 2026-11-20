# SPDX-License-Identifier: MIT OR Apache-2.0
"""Synthetic sequencer runs for offline tests. No game data, no images of the game."""
import copy
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import packet_format as pf  # noqa: E402

# The current canary packet (version 4, photo_v2, antialiasing pinned from
# round 26) and the measured versions, kept byte for byte with their results:
# version 3 (photo_v2, round 26) and version 2 (photo_v1, retired, round 25).
V4_PACKET = ROOT / 'packets' / 'nif_backface_cull_v1.signs' / 'v4' / 'packet.json'
V3_PACKET = ROOT / 'packets' / 'nif_backface_cull_v1.signs' / 'v3' / 'packet.json'
V2_PACKET = ROOT / 'packets' / 'nif_backface_cull_v1.signs' / 'packet.json'
CANARY_PACKET = V4_PACKET


def canary_packet():
    return json.loads(CANARY_PACKET.read_text())


def v3_packet():
    return json.loads(V3_PACKET.read_text())


def v2_packet():
    return json.loads(V2_PACKET.read_text())


def pinned(packet):
    """A test variant whose antialiasing value text counts as pinned (by its exact key)."""
    p = copy.deepcopy(packet)
    aa = p['requirements']['antialiasing']
    aa['value_text_pinned'] = True
    aa.setdefault('key', pf.antialiasing_key(aa))
    return p


def scalar(value):
    return dict(bits=pf.float32_word(value), decimal=value)


def camera(state_id=3, x=8192.0):
    row = lambda *v: [scalar(a) for a in v]
    return dict(complete=True, state_id=state_id, world_fov_raw=scalar(70.0), first_person_fov_raw=scalar(80.0),
                camera=dict(world=dict(rotation=[row(0, 1, 0), row(0, 0, 1), row(1, 0, 0)],
                                       rotation_padding=row(0, 0, 0), translation=row(x, 10200.0, 7.17578125),
                                       scale=scalar(1.0)),
                            world_to_camera_raw=[scalar(0.0)] * 16,
                            frustum_left_right_top_bottom_near_far=row(-0.8391, 0.8391, 0.472, -0.472, 15, 353840),
                            viewport_left_right_top_bottom=row(0, 1, 1, 0), orthographic=False),
                errors=[])


def image(red_fraction, shade=(118, 121, 124), width=1280, height=720, overlay=False, cursor=False):
    """Uniform gray frame; a share of the sign band painted sign-red.

    overlay darkens the lower part as round 25's console box did (about 70
    levels darker), from row 444, below the red lettering, so the criterion's
    in-frame contrast hardly moves; cursor paints a small green mark in the
    sign band, smaller than any mean tolerance.
    """
    import numpy as np
    a = np.zeros((height, width, 3), np.uint8)
    a[:] = shade
    if red_fraction:
        band = a[380:440, 300:980]
        mask = (np.arange(band.shape[1]) % 100) < int(round(red_fraction * 100))
        band[:, mask] = (196, 62, 48)
    if overlay:
        a[444:] = (a[444:].astype(int) * 2 // 5).astype(np.uint8)
    if cursor:
        a[364:376, 640:652] = (40, 230, 60)
    return a


def write_run(folder, packet, shots, settle=(41.0, 41.0), status='complete', position_error=0.0,
              repeat_camera_x=None, capture_state=3, packet_bytes=None, repeat_image=None,
              photo_camera_x=None, aa_value=None, executable=None):
    """Write packet.json, run.json and screenshots like a finished plugin run.

    `shots` maps step id to (photo_red_fraction, repeat_red_fraction).
    repeat_image passes keyword arguments to image() for every repeat
    (for example dict(overlay=True)).
    """
    from PIL import Image
    folder = Path(folder)
    (folder / 'screenshots').mkdir(parents=True, exist_ok=True)
    data = packet_bytes if packet_bytes is not None else (json.dumps(packet, indent=2) + '\n').encode()
    (folder / 'packet.json').write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    marks, screenshots, clock, n = [], [], 100.0, 0
    for part in pf.expand(packet):
        clock += 1.0
        mark = dict(index=len(marks), part=part['name'], role=part['role'], step=part['step'],
                    outcome='advanced', utc='2026-11-16T00:00:00.000Z', qpc_seconds=clock,
                    wall_seconds_since_pose=None, simulated_seconds_since_pose=None, game_days_passed=None,
                    timescale=None, player_position=None, player_parent_cell='07001000', camera=None,
                    roots=[], checks=[], messages=[])
        step = next((s for s in packet['steps'] if s['id'] == part['step']), None)
        if part['role'] == 'pose':
            pose_mark = len(marks)
            mark['observed'] = dict(pose_in_effect_at_pose_mark=False,
                                    timing_origin=dict(mark=pose_mark, role='pose'))
        if part['role'] == 'settle':
            clock += settle[0]
            mark.update(qpc_seconds=clock, wall_seconds_since_pose=settle[0],
                        simulated_seconds_since_pose=settle[1],
                        player_position=[scalar(v + (position_error if i == 0 else 0))
                                         for i, v in enumerate(step['position'])],
                        observed=dict(pose_in_effect=True, timing_origin=dict(mark=pose_mark, role='pose')))
        if part['role'] == 'free':
            pass
        if part['role'] == 'capture':
            clock += 4.0
            mark.update(qpc_seconds=clock, camera=camera(capture_state),
                        roots=[dict(form_id='07' + r, complete=True, world=None, errors=[]) for r in step['roots']])
        if part['role'] == 'photo':
            mark.update(camera=camera(3, photo_camera_x if photo_camera_x is not None else 8192.0))
        if part['role'] == 'repeat':
            mark.update(camera=camera(3, repeat_camera_x if repeat_camera_x is not None else 8192.0))
        if part['role'] == 'restore':
            mark.update(camera=camera(3))
        marks.append(mark)
        if part['shots'] and step['id'] in shots:
            shot = 'repeat' if part['role'] == 'repeat' else 'photo'
            fraction = shots[step['id']][0 if shot == 'photo' else 1]
            name = 'ScreenShot%d.png' % n
            n += 1
            extra = (repeat_image or {}) if shot == 'repeat' else {}
            Image.fromarray(image(fraction, **extra)).save(folder / 'screenshots' / name)
            raw = (folder / 'screenshots' / name).read_bytes()
            screenshots.append(dict(file=name, size=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                                    bound_to=dict(step=step['id'], shot=shot), status='bound',
                                    seen_at_mark=len(marks)))
    aa = packet['requirements']['antialiasing']
    aa_text = aa_value if aa_value is not None else aa if isinstance(aa, str) else aa['value_text']
    exe = executable or packet['requirements'].get('executable') or dict(file='Fallout4.exe', size=1, sha256='2' * 64)
    run = dict(run_schema=2, plugin_version=9, status=status,
               status_reasons=[] if status == 'complete' else ['synthetic abort'],
               packet=dict(id=packet['id'], version=packet['version'], sha256=digest,
                           plan_sha256='0' * 64),
               session_id='SYNTHETIC', run_id='synthetic-1', started_utc='2026-11-16T00:00:00.000Z',
               updated_utc='2026-11-16T00:00:00.000Z',
               environment=dict(game_version='1.10.163.0', f4se_version='0.6.23', f4se_release_index=25,
                                plugin_version=9,
                                plugin_dll=dict(file='osp_measure.dll', size=1, sha256='1' * 64),
                                executable=dict(exe),
                                f4se_runtime_dll=None,
                                bench_plugin=dict(file=packet['bench_plugin']['file'],
                                                  size=packet['bench_plugin']['size'],
                                                  sha256=packet['bench_plugin']['sha256']),
                                active_plugins=[dict(m, mod_index=i) for i, m in
                                                enumerate(packet['requirements']['active_plugins'])],
                                ini=[dict(key=pf.antialiasing_key(aa), found=True, type='string',
                                          value=aa_text, raw_bits_hex='0')],
                                system=dict(windows_version='10.0.19045', wine_version=None),
                                gpu=dict(adapter='synthetic'), client_size=[1280, 720],
                                errors=dict(f4se_runtime_dll='synthetic: not hashed')),
               preflight=dict(checks=[]), marks=marks, f4se_messages=[], screenshots=screenshots, pending_screenshots=[],
               next_part=None)
    (folder / 'run.json').write_text(json.dumps(run, indent=1) + '\n')
    return run


def with_canary(packet):
    """A packet variant with one canary step and canary criterion (for void tests)."""
    p = copy.deepcopy(packet)
    s = copy.deepcopy(p['steps'][0])
    s.update(id='canary_front', kind='canary')
    p['steps'].append(s)
    p['canaries'] = [dict(rule='synthetic_known_rule', expected='supported', criterion=dict(
        measures=dict(canary=dict(step='canary_front', stat='red_excess', target='sign_band',
                                  control='side_control'))),
        )]
    c = p['canaries'][0]['criterion']
    c.update(repeat_tolerance=1.0, controls=[],
             supported_if_all=[dict(measure='canary', op='>=', value=3.0)],
             refuted_if_any=[dict(measure='canary', op='<', value=1.0)])
    del p['canary_exemption']
    parts = pf.expand(p)
    p['expected_outputs'] = dict(parts=len(parts), marks=len(parts),
                                 screenshots=sum(x['shots'] for x in parts), steps=len(p['steps']))
    return p
