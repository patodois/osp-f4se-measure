# SPDX-License-Identifier: MIT OR Apache-2.0
"""Result format 1: step qualification, crop statistics and the verdict.

Every decision in a result is recomputed from numbers stored in the result
itself (`check`), so a CI can confirm a submitted verdict without images.
Images are never part of a result: only their hashes, sizes and statistics.
"""
import math
import struct

import packet_format as pf

RESULT_SCHEMA = 1
VERDICTS = ('supported', 'refuted', 'inconclusive', 'void')


def ini_for_requirement(requirements):
    """INI keys whose effective value decides a requirement, by exact name.

    Every other recorded key is evidence only; a key that was not found leaves
    its requirement unverified and never met, whatever near names (candidates)
    the plugin recorded beside it.
    """
    return {pf.antialiasing_key(requirements['antialiasing']): 'antialiasing'}


class ResultError(ValueError):
    pass


def word_value(bits):
    """Decode an eight-digit IEEE binary32 word; None for non-finite words."""
    if not isinstance(bits, str) or len(bits) != 8:
        raise ResultError('float word must be eight hexadecimal digits')
    value = struct.unpack('>f', bytes.fromhex(bits))[0]
    return value if math.isfinite(value) else None


# ---------------------------------------------------------------- statistics

def crop_statistics(rgb, rectangles, mask=None):
    """Encoded RGB8 statistics over the union of rectangles, mask applied.

    Overlapping rectangles count each pixel once. Population deviations
    (ddof=0). These are encoded levels, not linear light.
    """
    import numpy as np
    a = np.asarray(rgb)
    if a.dtype != np.uint8 or a.ndim != 3 or a.shape[2] != 3:
        raise ResultError('RGB8 image required')
    height, width = a.shape[:2]
    select = np.zeros((height, width), dtype=bool)
    for x0, y0, x1, y1 in rectangles:
        if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
            raise ResultError('crop rectangle outside the image')
        select[y0:y1, x0:x1] = True
    if mask is not None:
        select &= mask
    values = a[select].astype(np.float64)
    if not len(values):
        return dict(available=False, pixels=0)
    gray = values.mean(axis=1)
    mean = values.mean(axis=0)
    return dict(available=True, pixels=int(len(values)),
                mean_r=float(mean[0]), mean_g=float(mean[1]), mean_b=float(mean[2]),
                mean_gray=float(gray.mean()), std_gray=float(gray.std()),
                red_excess=float(mean[0] - (mean[1] + mean[2]) / 2))


def image_mask(kind, width, height):
    if kind == 'none':
        return None
    import native_photo
    return native_photo.corner_mask(width, height)


# ------------------------------------------------------------- qualification

def camera_words(camera):
    """Exact numeric camera words used for stability; pointers excluded."""
    if not isinstance(camera, dict) or camera.get('complete') is not True:
        return None
    c = camera.get('camera')
    if not isinstance(c, dict):
        return None
    w = c['world']
    bits = lambda xs: [x['bits'] for x in xs]
    return dict(state_id=camera.get('state_id'),
                fovs=[camera[k]['bits'] for k in ('world_fov_raw', 'first_person_fov_raw')],
                rotation=[bits(r) for r in w['rotation']], rotation_padding=bits(w['rotation_padding']),
                translation=bits(w['translation']), scale=w['scale']['bits'],
                world_to_camera_raw=bits(c['world_to_camera_raw']),
                frustum=bits(c['frustum_left_right_top_bottom_near_far']),
                viewport=bits(c['viewport_left_right_top_bottom']), orthographic=c['orthographic'])


def image_agreement(photo, repeat, rectangles, mask, threshold):
    """Pixels inside the crop whose largest channel difference exceeds threshold."""
    import numpy as np
    a, b = np.asarray(photo), np.asarray(repeat)
    if a.shape != b.shape:
        raise ResultError('photo and repeat differ in size')
    select = np.zeros(a.shape[:2], dtype=bool)
    for x0, y0, x1, y1 in rectangles:
        select[y0:y1, x0:x1] = True
    if mask is not None:
        select &= mask
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    return dict(pixels=int(select.sum()), threshold=int(threshold),
                pixels_over_threshold=int((select & (diff > threshold)).sum()))


def agreement_failures(record, checks):
    """Photo and repeat must agree inside the packet's own crops (image_checks).

    Both are taken with the same frozen camera, so a difference beyond the
    stated tolerance means something other than the scene changed between them:
    an interface overlay, a moving object or an image still converging.
    """
    shots = record.get('shots') or {}
    photo = (shots.get('photo') or {}).get('crops')
    repeat = (shots.get('repeat') or {}).get('crops')
    if not isinstance(photo, dict) or not isinstance(repeat, dict):
        return []          # already unqualified: a screenshot is missing or unmeasured
    failures = []
    agreement = record.get('image_agreement') or {}
    for crop in checks['crops']:
        a, b = photo.get(crop), repeat.get(crop)
        if not (a and b and a.get('available') and b.get('available')):
            failures.append('photo/repeat agreement: crop %s unavailable' % crop)
            continue
        for stat in checks['stats']:
            d = abs(a[stat] - b[stat])
            if d > checks['max_stat_difference']:
                failures.append('photo and repeat disagree in %s: %s differs by %.3f > %.3f' % (
                    crop, stat, d, checks['max_stat_difference']))
        n = (agreement.get(crop) or {}).get('pixels_over_threshold')
        if type(n) is not int:
            failures.append('photo/repeat pixel agreement not measured in %s' % crop)
        elif n > checks['max_pixels_over_threshold']:
            failures.append('photo and repeat disagree in %s: %d pixels differ by more than %d levels' % (
                crop, n, checks['pixel_threshold']))
    return failures


def qualify_step(step, record, tolerance, resolution, image_checks=None):
    """Return the list of failed qualification checks for one step record."""
    failures = []
    settle = record.get('settle') or {}
    if settle.get('outcome') != 'advanced':
        failures.append('settle mark never accepted')
    wall, simulated = settle.get('wall_seconds'), settle.get('simulated_seconds')
    if not isinstance(wall, (int, float)) or wall < step['settle_seconds']:
        failures.append('wall-clock settle shorter than %d s' % step['settle_seconds'])
    if step['simulated_time'] == 'require':
        if not isinstance(simulated, (int, float)):
            failures.append('simulated time unavailable but required')
        elif simulated < step['settle_seconds']:
            failures.append('simulated settle shorter than %d s' % step['settle_seconds'])
    words = record.get('position_words')
    if not (isinstance(words, list) and len(words) == 3):
        failures.append('player position not observed')
    else:
        values = [word_value(w) for w in words]
        wanted = [word_value(pf.float32_word(v)) for v in step['position']]
        if None in values or any(abs(a - b) > tolerance for a, b in zip(values, wanted)):
            failures.append('player position outside tolerance')
    free = record.get('free_wall_seconds')
    if not isinstance(free, (int, float)) or free < step['free_camera_seconds']:
        failures.append('free camera held shorter than %d s before capture' % step['free_camera_seconds'])
    capture, repeat = record.get('camera_capture'), record.get('camera_repeat')
    if not capture or capture.get('complete') is not True or capture.get('state_id') != 3:
        failures.append('capture camera incomplete or not free camera')
    if not repeat or repeat.get('complete') is not True:
        failures.append('repeat camera incomplete')
    elif capture and capture.get('words') != repeat.get('words'):
        failures.append('camera words changed between photo and repeat')
    if 'camera_photo' in record:          # photo_v2: the photograph has its own mark
        photo = record['camera_photo']
        if not photo or photo.get('complete') is not True:
            failures.append('photo camera incomplete')
        elif capture and capture.get('words') != photo.get('words'):
            failures.append('camera words changed between capture and photo')
    shots = record.get('shots') or {}
    for shot in pf.SHOTS:
        s = shots.get(shot)
        if not isinstance(s, dict) or s.get('status') != 'bound':
            failures.append(shot + ' screenshot not bound')
        elif [s.get('width'), s.get('height')] != list(resolution):
            failures.append(shot + ' screenshot is not %dx%d' % tuple(resolution))
        elif not isinstance(s.get('crops'), dict):
            failures.append(shot + ' crop statistics missing')
    if image_checks is not None:
        failures += agreement_failures(record, image_checks)
    return failures


# --------------------------------------------------------- criterion verdict

def _compare(left, op, right):
    return {'<': left < right, '<=': left <= right, '>': left > right, '>=': left >= right}[op]


def evaluate(criterion, steps, qualified):
    """Evaluate one criterion against per-step crop statistics.

    `steps` maps step id to {'photo': {crop: stats}, 'repeat': {crop: stats}}.
    `qualified` maps step id to its list of qualification failures.

    Measures are computed whenever crops exist, so a reader always sees the
    numbers. `verdict` is the decision: any unqualified step, missing crop,
    unstable repeat or failed control makes it inconclusive.
    `numbers_only_verdict` applies the same conditions while ignoring step
    qualification; it is never a decision, only what the numbers would say.
    """
    reasons, numeric_reasons, values = [], [], {}
    for name, m in sorted(criterion['measures'].items()):
        row = {}
        for shot in pf.SHOTS:
            crops = (steps.get(m['step']) or {}).get(shot) or {}
            t, c = crops.get(m['target']), crops.get(m['control'])
            if not (t and c and t.get('available') and c.get('available')):
                row = None
                break
            row[shot] = t[m['stat']] - c[m['stat']]
        if qualified.get(m['step']):
            reasons.append('%s: step %s unqualified (%s)' % (name, m['step'], '; '.join(qualified[m['step']])))
        if row is None:
            text = '%s: crop statistics unavailable' % name
            reasons.append(text)
            numeric_reasons.append(text)
            continue
        row['repeat_difference'] = abs(row['photo'] - row['repeat'])
        row['step_qualified'] = not qualified.get(m['step'])
        values[name] = row
        if row['repeat_difference'] > criterion['repeat_tolerance']:
            text = '%s: photo and repeat differ by %.6g > %.6g' % (
                name, row['repeat_difference'], criterion['repeat_tolerance'])
            reasons.append(text)
            numeric_reasons.append(text)

    def check(rows):
        out = []
        for row in rows:
            if row['measure'] not in values or row.get('ref', row['measure']) not in values:
                out.append(dict(row, holds=None))
                continue
            right = (row['factor'] * values[row['ref']]['photo'] + row.get('offset', 0)
                     if 'ref' in row else row['value'])
            out.append(dict(row, left=values[row['measure']]['photo'], right=right,
                            holds=_compare(values[row['measure']]['photo'], row['op'], right)))
        return out
    controls = check(criterion['controls'])
    supported = check(criterion['supported_if_all'])
    refuted = check(criterion['refuted_if_any'])
    for row in controls:
        if row['holds'] is False:
            text = 'control failed: %s %s %.6g (observed %.6g)' % (row['measure'], row['op'], row['right'], row['left'])
            reasons.append(text)
            numeric_reasons.append(text)

    def decide(blocking, notes):
        if blocking:
            return 'inconclusive'
        if any(r['holds'] is None for r in supported + refuted):
            notes.append('a condition could not be evaluated')
            return 'inconclusive'
        all_supported = all(r['holds'] for r in supported)
        any_refuted = any(r['holds'] for r in refuted)
        if all_supported and any_refuted:
            notes.append('supported and refuted conditions both hold')
            return 'inconclusive'
        if all_supported:
            return 'supported'
        if any_refuted:
            return 'refuted'
        notes.append('neither the supported nor the refuted condition set holds')
        return 'inconclusive'
    numbers_only = decide(list(numeric_reasons), [])
    verdict = decide(list(reasons), reasons)
    return dict(verdict=verdict, numbers_only_verdict=numbers_only, reasons=reasons, measures=values,
                controls=controls, supported_if_all=supported, refuted_if_any=refuted)


def requirement_status(result):
    """Map each requirement to met / violated / unverified from result numbers."""
    req, env = result['requirements'], result['environment']
    status = {}

    def value(key):
        field = env.get(key) or {}
        return field.get('value')
    for key, want in (('game_version', req['game_version']), ('f4se_version', req['f4se_version']),
                      ('f4se_release_index', req['f4se_release_index'])):
        got = value(key)
        status[key] = 'unverified' if got is None else 'met' if got == want else 'violated'
    plugins = value('active_plugins')
    if plugins is None:
        status['active_plugins'] = 'unverified'
    else:
        got = [(m['name'].lower(), m['kind']) for m in plugins]
        want = [(m['name'].lower(), m['kind']) for m in req['active_plugins']]
        status['active_plugins'] = 'met' if got == want else 'violated'
    bench = value('bench_plugin')
    b = result['bench_plugin']
    if bench is None:
        status['bench_plugin'] = 'unverified'
    else:
        status['bench_plugin'] = 'met' if (bench.get('size'), bench.get('sha256')) == (b['size'], b['sha256']) else 'violated'
    pv = value('plugin_version')
    status['plugin_min_version'] = 'unverified' if pv is None else 'met' if pv >= req['plugin_min_version'] else 'violated'
    sizes = value('screenshot_sizes')
    if not sizes:
        status['resolution'] = 'unverified'
    else:
        status['resolution'] = 'met' if all(s == req['resolution'] for s in sizes) else 'violated'
    ini = {row['key']: row for row in (value('ini') or [])}
    want = req['antialiasing']
    aa = ini.get(pf.antialiasing_key(want))
    if not aa or aa.get('found') is not True or not isinstance(aa.get('value'), str):
        status['antialiasing'] = 'unverified'
    elif isinstance(want, str):           # packet versions 1-2: the mode name only
        want = '' if want == 'none' else want
        status['antialiasing'] = 'met' if aa['value'].strip().lower() == want.lower() else 'violated'
    elif aa['value'] == want['value_text']:
        status['antialiasing'] = 'met' if want['value_text_pinned'] else 'unverified'
    elif aa['value'] in want['other_mode_texts'].values() or want['value_text_pinned']:
        status['antialiasing'] = 'violated'
    else:                                 # an unknown text while the expected one is unpinned
        status['antialiasing'] = 'unverified'
    if 'executable' in req:
        exe = value('executable')
        x = req['executable']
        status['executable'] = ('unverified' if exe is None else
                                'met' if (exe.get('size'), exe.get('sha256')) == (x['size'], x['sha256']) else 'violated')
    data = value('data_inventory')
    if data is None:
        status['data_unmodded'] = 'unverified'
    else:
        status['data_unmodded'] = 'met' if not data.get('unexpected_directories') else 'violated'
    return status


def requirement_notes(result):
    """Plain words on the INI-backed requirements and every key that was not found."""
    env = (result['environment'].get('ini') or {}).get('value') or []
    rows = {row.get('key'): row for row in env}
    notes = []
    deciding = ini_for_requirement(result['requirements'])
    for key in result['requirements']['ini_keys']:
        row = rows.get(key)
        uses = deciding.get(key)
        if row is None or row.get('found') is not True:
            notes.append(dict(key=key, found=False, reason=(row or {}).get('reason', 'not recorded by the plugin'),
                              candidates=[c.get('name') for c in (row or {}).get('candidates') or []],
                              weakens=uses or 'nothing decided: recorded only'))
    want = result['requirements']['antialiasing']
    key = pf.antialiasing_key(want)
    aa = rows.get(key) or {}
    text = want if isinstance(want, str) else want['value_text']
    pinned = isinstance(want, str) or want['value_text_pinned']
    return dict(missing_ini_keys=notes, antialiasing=dict(
        key=key, observed=aa.get('value') if aa.get('found') else None, required_text=text,
        required_text_pinned=pinned))


def verdict(result):
    """Recompute the whole decision from the numbers in a result."""
    step_defs = {s['id']: s for s in result['steps_requested']}
    records = {s['id']: s for s in result['steps']}
    checks = result.get('image_checks')
    qualified = {k: qualify_step(step_defs[k], records.get(k, {}), result['position_tolerance'],
                                 result['requirements']['resolution'], checks) for k in step_defs}
    crops = {k: {shot: ((records.get(k, {}).get('shots') or {}).get(shot) or {}).get('crops') or {}
                 for shot in pf.SHOTS} for k in step_defs}
    void, inadmissible = [], []
    if result['run']['status'] != 'complete':
        void.append('run status is %s' % result['run']['status'])
    if result['packet']['sha256'] != result['run'].get('packet_sha256'):
        void.append('run was made with a different packet')
    requirements = requirement_status(result)
    for key, state in sorted(requirements.items()):
        if state == 'violated':
            void.append('requirement violated: ' + key)
        elif state == 'unverified':
            inadmissible.append('requirement unverified: ' + key)
    main = evaluate(result['criterion'], crops, qualified)
    canaries = []
    for c in result['canaries']:
        e = evaluate(c['criterion'], crops, qualified)
        canaries.append(dict(rule=c['rule'], expected=c['expected'], verdict=e['verdict'],
                             numbers_only_verdict=e['numbers_only_verdict'],
                             passed=e['verdict'] == c['expected'], reasons=e['reasons'],
                             measures=e['measures']))
        if e['verdict'] != c['expected']:
            void.append('canary %s gave %s, expected %s' % (c['rule'], e['verdict'], c['expected']))
    if result['kind'] != 'sequencer_run':
        inadmissible.append('result kind is %s, not sequencer_run' % result['kind'])
    final = 'void' if void else main['verdict']
    return dict(verdict=final, criterion_verdict=main['verdict'],
                numbers_only_verdict=main['numbers_only_verdict'], void_reasons=void,
                admissible=not void and not inadmissible, admissibility_reasons=inadmissible,
                requirements=requirements, requirement_notes=requirement_notes(result),
                qualification=qualified, evaluation=main, canaries=canaries)


RESULT_KEYS = ('result_schema', 'kind', 'packet', 'rule', 'question', 'requirements', 'bench_plugin',
               'position_tolerance', 'steps_requested', 'crops', 'mask', 'criterion', 'canaries', 'run',
               'environment', 'steps', 'decision', 'verdict', 'analysis')


RESULT_OPTIONAL = ('notes', 'image_checks')


def validate_result(r):
    """Structural checks; the decision itself is checked by recomputation."""
    errors = pf._keys(r, RESULT_KEYS, RESULT_OPTIONAL, 'result')
    if errors:
        return errors
    if r['result_schema'] != RESULT_SCHEMA:
        errors.append('result_schema must be 1')
    if r['kind'] not in ('sequencer_run', 'retrospective'):
        errors.append('kind must be sequencer_run or retrospective')
    if r['verdict'] not in VERDICTS:
        errors.append('verdict must be one of ' + ', '.join(VERDICTS))
    pk = r['packet']
    if not isinstance(pk, dict) or not pf.SHA_RE.fullmatch(str(pk.get('sha256'))):
        errors.append('packet.sha256 missing')
    env = r['environment']
    if not isinstance(env, dict):
        errors.append('environment must be an object')
    else:
        for key, field in env.items():
            if pf._keys(field, ('value', 'filled_by', 'reason'), (), 'environment.' + key):
                errors.append('environment.%s must be {value, filled_by, reason}' % key)
            elif field['filled_by'] not in ('plugin', 'tool', 'person', 'none'):
                errors.append('environment.%s.filled_by is invalid' % key)
            elif field['value'] is None and not field['reason']:
                errors.append('environment.%s is null without a reason' % key)

    def no_images(x, path):
        if isinstance(x, dict):
            for k, v in x.items():
                if k in ('pixels_base64', 'image', 'png'):
                    errors.append('image data is not allowed in a result: ' + path + '.' + k)
                no_images(v, path + '.' + k)
        elif isinstance(x, list):
            for i, v in enumerate(x):
                no_images(v, '%s[%d]' % (path, i))
        elif isinstance(x, str) and len(x) > 4096:
            errors.append('oversized string (possible embedded image) at ' + path)
    no_images(r, 'result')
    return errors
