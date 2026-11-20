#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Offline measurement-packet tool. Reads files only; never starts the game.

  validate <packet.json>                     check a packet and its expansion
  build <packet.json> <new-folder> [--bench ESP]
                                             write the files to copy into the game
  analyze <run-folder> [--packet P] [--screenshots DIR] [--game-dir DIR] [--system JSON] [--out FILE]
          [--retrospective]                  fill result.json from run.json and screenshots;
                                             --retrospective marks a result that is never admissible
                                             (for example a stored run read against a later version)
  check <result.json> [--packet P]           recompute the verdict from numbers alone
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packet_format as pf  # noqa: E402
import result_format as rf  # noqa: E402

TOOL_VERSION = 2
DATA_DIRECTORIES_ALLOWED = ('f4se', 'scripts', 'strings', 'video')


def sha256_file(path, limit=None):
    h, n = hashlib.sha256(), 0
    with open(path, 'rb') as f:
        while True:
            block = f.read(1 << 20)
            if not block:
                break
            n += len(block)
            if limit and n > limit:
                raise rf.ResultError('file larger than %d bytes: %s' % (limit, Path(path).name))
            h.update(block)
    return h.hexdigest(), n


def env(value, filled_by, reason=None):
    if value is None and not reason:
        raise ValueError('null environment value needs a reason')
    return dict(value=value, filled_by=filled_by if value is not None else 'none', reason=reason)


def plugin_environment(run):
    e = run.get('environment') or {}
    missing = e.get('errors') or {}
    out = {}
    for key in ('game_version', 'f4se_version', 'f4se_release_index', 'plugin_version', 'plugin_dll',
                'executable', 'f4se_runtime_dll', 'bench_plugin', 'active_plugins', 'ini', 'system',
                'gpu', 'client_size', 'start_timings'):
        value = e.get(key)
        out[key] = env(value, 'plugin', None if value is not None else
                       missing.get(key, 'not recorded by the plugin'))
    return out


def tool_environment(packet, game_dir):
    out = {}
    if game_dir is None:
        reason = 'no --game-dir given to the offline tool'
        out['hashed_files'] = env(None, 'tool', reason)
        out['data_inventory'] = env(None, 'tool', reason)
        return out
    data = Path(game_dir) / 'Data'
    rows = []
    for name in packet['requirements']['hashed_files']:
        path = data / name
        if path.is_file():
            digest, size = sha256_file(path)
            rows.append(dict(file=name, size=size, sha256=digest))
        else:
            rows.append(dict(file=name, size=None, sha256=None, missing=True))
    out['hashed_files'] = env(rows, 'tool')
    if not data.is_dir():
        out['data_inventory'] = env(None, 'tool', 'Data folder not found under --game-dir')
        return out
    directories = sorted(p.name for p in data.iterdir() if p.is_dir())
    files = sorted(p.name for p in data.iterdir() if p.is_file())
    active = {m['name'].lower() for m in packet['requirements']['active_plugins']}
    plugins = [f for f in files if f.lower().endswith(('.esm', '.esp', '.esl'))]
    out['data_inventory'] = env(dict(
        directories=directories,
        unexpected_directories=[d for d in directories if d.lower() not in DATA_DIRECTORIES_ALLOWED],
        inactive_plugin_files=[f for f in plugins if f.lower() not in active],
        archives=len([f for f in files if f.lower().endswith('.ba2')]),
        rule='Data may hold only the directories ' + ', '.join(DATA_DIRECTORIES_ALLOWED)), 'tool')
    return out


def step_records(packet, run):
    """Collect per-step numbers from the plugin's mark list."""
    marks = run.get('marks') or []
    shots_by_step = {}
    for s in run.get('screenshots') or []:
        b = s.get('bound_to')
        if s.get('status') == 'bound' and b:
            shots_by_step.setdefault(b['step'], {})[b['shot']] = s
    records = []
    for step in packet['steps']:
        mine = [m for m in marks if m.get('step') == step['id']]
        by_role = lambda role: [m for m in mine if m.get('role') == role]
        settles = by_role('settle')
        settle = next((m for m in reversed(settles) if m.get('outcome') == 'advanced'),
                      settles[-1] if settles else None)
        free = (by_role('free') or [None])[-1]
        capture = (by_role('capture') or [None])[-1]
        photo = (by_role('photo') or [None])[-1]        # photo_v2 only
        repeat = (by_role('repeat') or [None])[-1]
        cam = lambda m: None if not m or not m.get('camera') else dict(
            complete=m['camera'].get('complete'), state_id=m['camera'].get('state_id'),
            words=rf.camera_words(m['camera']))
        position = settle.get('player_position') if settle else None
        record = dict(
            id=step['id'], kind=step['kind'],
            requested=dict(position=step['position'], angles=step['angles'],
                           position_words=[pf.float32_word(v) for v in step['position']]),
            settle=None if not settle else dict(
                outcome=settle.get('outcome'), attempts=len(settles),
                wall_seconds=settle.get('wall_seconds_since_pose'),
                simulated_seconds=settle.get('simulated_seconds_since_pose'),
                timing_origin=(settle.get('observed') or {}).get('timing_origin')),
            position_words=[w['bits'] for w in position] if position else None,
            free_wall_seconds=(capture['qpc_seconds'] - free['qpc_seconds']
                               if capture and free else None),
            camera_capture=cam(capture), camera_repeat=cam(repeat),
            roots=capture.get('roots') if capture else None,
            shots={})
        if any(m.get('role') == 'photo' for m in marks):
            record['camera_photo'] = cam(photo)
            record['photo_to_repeat_seconds'] = (repeat['qpc_seconds'] - photo['qpc_seconds']
                                                 if photo and repeat else None)
        for shot in pf.SHOTS:
            s = shots_by_step.get(step['id'], {}).get(shot)
            record['shots'][shot] = (dict(status='missing') if s is None else
                                     dict(status='bound', file=s['file'], size=s['size'], sha256=s['sha256']))
        records.append(record)
    return records


def measure_shots(packet, records, screenshot_dir):
    import numpy as np
    from PIL import Image
    sizes = []
    checks = packet.get('image_checks')
    for record in records:
        pixels = {}
        for shot, s in record['shots'].items():
            if s.get('status') != 'bound':
                continue
            path = Path(screenshot_dir) / s['file']
            if not path.is_file():
                s.update(status='file_missing')
                continue
            digest, size = sha256_file(path, 256 * 1024 * 1024)
            if (digest, size) != (s['sha256'], s['size']):
                s.update(status='hash_mismatch')
                continue
            with Image.open(path) as im:
                rgb = np.asarray(im.convert('RGB'))
            height, width = rgb.shape[:2]
            s.update(width=width, height=height)
            sizes.append([width, height])
            mask = rf.image_mask(packet['mask'], width, height)
            if [width, height] == packet['requirements']['resolution']:
                s['crops'] = {name: rf.crop_statistics(rgb, rects, mask)
                              for name, rects in sorted(packet['crops'].items())}
                pixels[shot] = (rgb, mask)
        if checks is not None and len(pixels) == 2:
            (photo, mask), (repeat, _) = pixels['photo'], pixels['repeat']
            record['image_agreement'] = {
                crop: rf.image_agreement(photo, repeat, packet['crops'][crop], mask, checks['pixel_threshold'])
                for crop in checks['crops']}
    return sizes


def assemble(packet, packet_sha256, run, run_sha256, kind, screenshot_dir, game_dir, system):
    records = step_records(packet, run)
    sizes = measure_shots(packet, records, screenshot_dir)
    environment = plugin_environment(run)
    environment.update(tool_environment(packet, game_dir))
    environment['screenshot_sizes'] = env(sizes or None, 'tool', None if sizes else 'no bound screenshot was readable')
    declared = json.loads(Path(system).read_text()) if system else {}
    for key in ('gpu_driver', 'gpu_vendor'):
        environment[key] = env(declared.get(key), 'person', None if declared.get(key) else
                               'not readable by the plugin; may be declared with --system')
    result = dict(
        result_schema=rf.RESULT_SCHEMA, kind=kind,
        packet=dict(id=packet['id'], version=packet['version'], sha256=packet_sha256),
        rule=packet['rule'], question=packet['question'], requirements=packet['requirements'],
        bench_plugin=packet['bench_plugin'], position_tolerance=packet['position_tolerance'],
        steps_requested=packet['steps'], crops=packet['crops'], mask=packet['mask'],
        criterion=packet['criterion'], canaries=packet['canaries'],
        run=dict(status=run.get('status'), status_reasons=run.get('status_reasons', []),
                 run_schema=run.get('run_schema'), f4se_messages=run.get('f4se_messages'),
                 run_id=run.get('run_id'), session_id=run.get('session_id'),
                 packet_sha256=(run.get('packet') or {}).get('sha256'), run_json_sha256=run_sha256,
                 unexpected_screenshots=[s for s in run.get('screenshots') or [] if s.get('status') != 'bound'],
                 pending_screenshots=run.get('pending_screenshots', [])),
        environment=environment, steps=records, decision=None, verdict='void',
        analysis=dict(tool='osp_packet.py', tool_version=TOOL_VERSION, images_included=False))
    if 'image_checks' in packet:
        result['image_checks'] = packet['image_checks']
    decision = rf.verdict(result)
    result['decision'] = decision
    result['verdict'] = decision['verdict']
    return result


def analyze(folder, packet_path=None, screenshots=None, game_dir=None, system=None, kind='sequencer_run', out=None):
    """Write result.json in the run folder, or at `out` (for evidence that must stay untouched)."""
    folder = Path(folder)
    run_bytes = (folder / 'run.json').read_bytes()
    run = json.loads(run_bytes)
    packet_path = Path(packet_path) if packet_path else folder / 'packet.json'
    packet, digest = pf.load(packet_path)
    errors = pf.validate_packet(packet, retired_ok=True)
    if errors:
        raise pf.PacketError('; '.join(errors))
    result = assemble(packet, digest, run, hashlib.sha256(run_bytes).hexdigest(), kind,
                      Path(screenshots) if screenshots else folder / 'screenshots', game_dir, system)
    problems = rf.validate_result(result)
    if problems:
        raise rf.ResultError('; '.join(problems))
    target = Path(out) if out else folder / 'result.json'
    if out and target.exists():
        raise rf.ResultError('refusing to overwrite ' + target.name)
    target.write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')
    return result


def check(result_path, packet_path=None):
    result = json.loads(Path(result_path).read_text())
    problems = rf.validate_result(result)
    if packet_path:
        packet, digest = pf.load(packet_path)
        if digest != result['packet']['sha256']:
            problems.append('result names packet %s, not %s' % (result['packet']['sha256'], digest))
        for key in ('criterion', 'canaries', 'requirements', 'crops', 'mask', 'position_tolerance',
                    'bench_plugin', 'image_checks'):
            if result.get(key) != packet.get(key):
                problems.append('result copy of %s differs from the packet' % key)
        if result.get('steps_requested') != packet.get('steps'):
            problems.append('result copy of steps differs from the packet')
    if problems:
        return dict(ok=False, problems=problems)
    decision = rf.verdict(result)
    stored = result['decision']
    mismatch = [k for k in ('verdict', 'criterion_verdict', 'numbers_only_verdict', 'void_reasons', 'admissible')
                if decision[k] != stored.get(k)]
    if decision['verdict'] != result['verdict']:
        mismatch.append('top-level verdict')
    return dict(ok=not mismatch, problems=['recomputed %s differs' % k for k in mismatch],
                verdict=decision['verdict'], admissible=decision['admissible'])


def summary(result):
    d = result['decision']
    lines = ['packet %s v%d  rule %s' % (result['packet']['id'], result['packet']['version'], result['rule']),
             'verdict: %s (criterion: %s; numbers only, ignoring step qualification: %s)  admissible: %s' % (
                 d['verdict'], d['criterion_verdict'], d['numbers_only_verdict'], d['admissible'])]
    for name, v in sorted(d['evaluation']['measures'].items()):
        lines.append('  %-12s photo %+.4f  repeat %+.4f%s' % (name, v['photo'], v['repeat'],
                                                         '' if v['step_qualified'] else '  (step unqualified)'))
    for r in d['void_reasons']:
        lines.append('  void: ' + r)
    for r in d['evaluation']['reasons']:
        # A step's failures are listed once below, not after every measure of that step.
        lines.append('  inconclusive: ' + (r.split(' (')[0] if ' unqualified (' in r else r))
    for r in d['admissibility_reasons']:
        lines.append('  not admissible: ' + r)
    for step, failures in sorted(d['qualification'].items()):
        for f in failures:
            lines.append('  unqualified %s: %s' % (step, f))
    for row in (d.get('requirement_notes') or {}).get('missing_ini_keys', []):
        lines.append('  INI key not found: %s (weakens: %s)' % (row['key'], row['weakens']))
    return '\n'.join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    v = sub.add_parser('validate')
    v.add_argument('packet')
    b = sub.add_parser('build')
    b.add_argument('packet')
    b.add_argument('out')
    b.add_argument('--bench')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('--packet')
    a.add_argument('--screenshots')
    a.add_argument('--game-dir')
    a.add_argument('--system')
    a.add_argument('--out', help='write result.json here instead of the run folder')
    a.add_argument('--retrospective', action='store_true',
                   help='kind retrospective: never admissible, for evidence not made with this packet')
    c = sub.add_parser('check')
    c.add_argument('result')
    c.add_argument('--packet')
    args = ap.parse_args(argv)
    try:
        if args.command == 'validate':
            packet, digest = pf.load(args.packet)
            errors = pf.validate_packet(packet)
            if errors:
                print('\n'.join('INVALID: ' + e for e in errors))
                return 1
            parts = pf.expand(packet)
            print('VALID: %s v%d sha256 %s; %d parts, %d screenshots' % (
                packet['id'], packet['version'], digest, len(parts), sum(x['shots'] for x in parts)))
            return 0
        if args.command == 'build':
            info = pf.build_install(args.packet, args.out, args.bench)
            print('BUILT: %d part files; packet sha256 %s; copy %s into the game folder' % (
                info['parts'], info['packet_sha256'], info['root']))
            return 0
        if args.command == 'analyze':
            result = analyze(args.folder, args.packet, args.screenshots, args.game_dir, args.system,
                             kind='retrospective' if args.retrospective else 'sequencer_run', out=args.out)
            print(summary(result))
            return 0
        if args.command == 'check':
            r = check(args.result, args.packet)
            print(('OK: verdict %s, admissible %s' % (r['verdict'], r['admissible'])) if r['ok']
                  else '\n'.join('FAIL: ' + p for p in r['problems']))
            return 0 if r['ok'] else 1
    except (pf.PacketError, rf.ResultError, OSError, ValueError, KeyError) as e:
        print('ERROR: %s' % e)
        return 2
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
