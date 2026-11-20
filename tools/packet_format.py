# SPDX-License-Identifier: MIT OR Apache-2.0
"""Measurement packet format 1: validation, part expansion and plan compilation.

A packet is data only. Its criterion is fixed before measurement and is
evaluated by result_format.py. The plugin never reads packet.json fields; it
reads plan.txt, compiled here, and checks that plan.txt names the SHA-256 of
the packet.json beside it. The command closed list below is mirrored in
include/osp/sequencer.hpp and both are tested against the same corpus.
"""
import hashlib
import json
import math
import re
import struct
from pathlib import Path

PACKET_SCHEMA = 1
PLAN_FORMAT = 2
TEMPLATE = 'photo_v2'
# photo_v1 (packet versions 1 and 2) put `tm` after `Screenshot` in the repeat
# part; round 25 showed the game takes the picture after the whole file has run,
# so that repeat photographed the interface. Its packets stay readable for the
# analysis of stored runs, but nothing can be built from them.
RETIRED_TEMPLATES = ('photo_v1',)
MARK = 'ospsnap'
ID_RE = re.compile(r'[a-z0-9][a-z0-9_.-]{0,63}')
STEP_RE = re.compile(r'[a-z0-9][a-z0-9_]{0,47}')
RULE_RE = re.compile(r'[a-z0-9][a-z0-9_]{0,95}')
PREFIX_RE = re.compile(r'[a-z]{1,8}')
EDITOR_ID_RE = re.compile(r'[A-Za-z0-9_]{1,64}')
LOCAL_ID_RE = re.compile(r'[0-9A-F]{6}')
SHA_RE = re.compile(r'[0-9a-f]{64}')
# A setting name with an optional section (mirrors ini_name in sequencer.hpp).
# Round 26 found the autosave settings under names without a section.
INI_KEY_RE = re.compile(r'[A-Za-z][A-Za-z0-9 _]{0,63}(:[A-Za-z0-9_]{1,32})?')
FILE_RE = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_ .-]{0,94}')
POSITION_RE = re.compile(r'-?[0-9]{1,7}(\.[0-9]{1,9})?')
ANGLE_RE = re.compile(r'-?[0-9]{1,3}')
STATS = ('mean_r', 'mean_g', 'mean_b', 'mean_gray', 'std_gray', 'red_excess')
AA_MODES = ('FXAA', 'TAA', 'none')
OPS = ('<', '<=', '>', '>=')
SHOTS = ('photo', 'repeat')
MAX_STEPS = 40
MAX_BAT_LINES = 16
# The closed list. Arguments are checked per verb; anything else is refused.
CLOSED_LIST = ('player.setpos', 'player.setangle', 'tfc', 'tm', 'tcl',
               'screenshot', 'coc', MARK)


class PacketError(ValueError):
    pass


def validate_command(line, cell=None):
    """Return (verb, args) for one allowed console line or raise PacketError.

    Verbs are case-insensitive as typed in the console; arguments are exact.
    `coc` is accepted only with the packet's declared bench cell EditorID.
    """
    if not isinstance(line, str) or not line or len(line) > 96:
        raise PacketError('command must be a non-empty line of at most 96 characters')
    if any(not (32 <= ord(c) < 127) for c in line) or ';' in line or '"' in line:
        raise PacketError('command must be printable ASCII without ; or quotes')
    parts = line.split(' ')
    if any(not p for p in parts):
        raise PacketError('single spaces only, no leading or trailing space')
    verb, args = parts[0].lower(), parts[1:]
    if verb not in CLOSED_LIST:
        raise PacketError('command outside the closed list: ' + parts[0])
    if verb == 'player.setpos':
        if len(args) != 2 or args[0] not in ('x', 'y', 'z') or not POSITION_RE.fullmatch(args[1]):
            raise PacketError('player.setpos takes one axis and one decimal')
    elif verb == 'player.setangle':
        if (len(args) != 2 or args[0] not in ('x', 'y', 'z') or not ANGLE_RE.fullmatch(args[1])
                or not -360 <= int(args[1]) <= 360):
            raise PacketError('player.setangle takes one axis and an integer in [-360, 360]')
    elif verb == 'tfc':
        if args not in ([], ['1']):
            raise PacketError('tfc takes no argument or 1')
    elif verb == 'coc':
        if len(args) != 1 or cell is None or args[0] != cell:
            raise PacketError('coc is allowed only with the packet bench cell')
    elif args:
        raise PacketError(parts[0] + ' takes no argument')
    return verb, args


def float32_word(value):
    """IEEE binary32 bits of the nearest float32, as the plugin compares them."""
    if not math.isfinite(value) or abs(value) > 3.4e38:
        raise PacketError('finite float32 value required')
    return struct.pack('>f', value).hex().upper()


def decimal_text(value):
    """Position text exactly as Round24 typed it: nine decimals, trimmed."""
    text = format(value, '.9f').rstrip('0').rstrip('.')
    if text in ('-0', ''):
        text = '0'
    if not POSITION_RE.fullmatch(text):
        raise PacketError('position outside the closed-list decimal form: %r' % value)
    return text


def _number(v):
    return type(v) in (int, float) and math.isfinite(v)


def _rect_ok(r, width, height):
    return (isinstance(r, list) and len(r) == 4 and all(type(v) is int for v in r)
            and 0 <= r[0] < r[2] <= width and 0 <= r[1] < r[3] <= height)


def _keys(obj, required, optional=(), where='packet'):
    errors = []
    if not isinstance(obj, dict):
        return [where + ' must be an object']
    missing = [k for k in required if k not in obj]
    extra = [k for k in obj if k not in required and k not in optional]
    if missing:
        errors.append(where + ' missing ' + ', '.join(missing))
    if extra:
        errors.append(where + ' has unknown fields ' + ', '.join(sorted(extra)))
    return errors


TOP = ('packet_schema', 'id', 'version', 'rule', 'rule_status_before', 'question',
       'estimated_minutes', 'requirements', 'bench_plugin', 'cell', 'bat_prefix',
       'template', 'screenshot_base_name', 'position_tolerance', 'steps', 'mask',
       'crops', 'criterion', 'canaries', 'expected_outputs')
TOP_OPTIONAL = ('canary_exemption', 'notes', 'image_checks')
REQUIREMENTS = ('game_version', 'f4se_version', 'f4se_release_index', 'plugin_min_version',
                'active_plugins', 'resolution', 'antialiasing', 'data_unmodded', 'hashed_files',
                'ini_keys')
# Required from photo_v2 on; optional only so that version 2 stays readable.
REQUIREMENTS_OPTIONAL = ('executable',)
ANTIALIASING = ('mode', 'value_text', 'value_text_pinned', 'other_mode_texts', 'evidence')
# `key` names the exact setting whose effective value decides the requirement.
# Version 3 has none and means DEFAULT_AA_KEY; a pinned value text needs it
# (round 26 found the setting only as SAntiAliasing:Display).
ANTIALIASING_OPTIONAL = ('key',)
DEFAULT_AA_KEY = 'sAntiAliasing:Display'
IMAGE_CHECKS = ('crops', 'stats', 'max_stat_difference', 'pixel_threshold', 'max_pixels_over_threshold')
STEP = ('id', 'kind', 'position', 'angles', 'settle_seconds', 'simulated_time',
        'free_camera_seconds', 'roots')
STEP_OPTIONAL = ('reference', 'notes')


def _criterion_errors(c, steps, crops, where):
    errors = _keys(c, ('measures', 'repeat_tolerance', 'controls', 'supported_if_all',
                       'refuted_if_any'), ('notes',), where)
    if errors:
        return errors
    measures = c['measures']
    if not isinstance(measures, dict) or not measures:
        return [where + '.measures must be a non-empty object']
    for name, m in measures.items():
        w = where + '.measures.' + str(name)
        if not STEP_RE.fullmatch(str(name)):
            errors.append(w + ' has an invalid name')
        e = _keys(m, ('step', 'stat', 'target', 'control'), (), w)
        if e:
            errors += e
            continue
        if m['step'] not in steps:
            errors.append(w + ' names an unknown step')
        if m['stat'] not in STATS:
            errors.append(w + ' has an unknown statistic')
        for k in ('target', 'control'):
            if m[k] not in crops:
                errors.append(w + ' names an unknown crop for ' + k)
        if m['target'] == m['control']:
            errors.append(w + ' compares a crop with itself')
    if not _number(c['repeat_tolerance']) or c['repeat_tolerance'] < 0:
        errors.append(where + '.repeat_tolerance must be a finite number >= 0')

    def conditions(key, allow_empty):
        out = []
        rows = c[key]
        if not isinstance(rows, list) or (not rows and not allow_empty):
            return [where + '.' + key + ' must be a ' + ('' if allow_empty else 'non-empty ') + 'list']
        for i, row in enumerate(rows):
            w = '%s.%s[%d]' % (where, key, i)
            if not isinstance(row, dict):
                out.append(w + ' must be an object')
                continue
            relative = 'ref' in row
            e = _keys(row, ('measure', 'op') + (('ref', 'factor') if relative else ('value',)),
                      ('offset',) if relative else (), w)
            if e:
                out += e
                continue
            if row['measure'] not in measures:
                out.append(w + ' names an unknown measure')
            if row['op'] not in OPS:
                out.append(w + ' has an unknown operator')
            if relative:
                if row['ref'] not in measures or row['ref'] == row['measure']:
                    out.append(w + ' needs another known measure as ref')
                if not _number(row['factor']) or not _number(row.get('offset', 0)):
                    out.append(w + ' needs finite factor and offset')
            elif not _number(row['value']):
                out.append(w + ' needs a finite value')
        return out
    errors += conditions('controls', True)
    errors += conditions('supported_if_all', False)
    errors += conditions('refuted_if_any', False)
    return errors


def antialiasing_key(aa):
    """The exact setting name whose effective value decides the antialiasing requirement."""
    return aa['key'] if isinstance(aa, dict) and 'key' in aa else DEFAULT_AA_KEY


def antialiasing_errors(aa, ini_keys=None):
    """A plain mode (packet versions 1-2) or the photo_v2 object naming the value text."""
    if isinstance(aa, str):
        return [] if aa in AA_MODES else ['antialiasing must be FXAA, TAA or none']
    errors = _keys(aa, ANTIALIASING, ANTIALIASING_OPTIONAL, 'requirements.antialiasing')
    if errors:
        return errors
    if 'key' in aa and (not isinstance(aa['key'], str) or not INI_KEY_RE.fullmatch(aa['key'])):
        errors.append('requirements.antialiasing.key must be a setting name')
    elif aa.get('value_text_pinned') is True and 'key' not in aa:
        errors.append('requirements.antialiasing.key is required once the value text is pinned')
    elif isinstance(ini_keys, list) and antialiasing_key(aa) not in ini_keys:
        errors.append('requirements.antialiasing.key %s must be listed in ini_keys' % antialiasing_key(aa))
    text = lambda v: isinstance(v, str) and len(v) <= 32 and all(32 <= ord(c) < 127 for c in v)
    if aa['mode'] not in AA_MODES:
        errors.append('requirements.antialiasing.mode must be FXAA, TAA or none')
    if not text(aa['value_text']):
        errors.append('requirements.antialiasing.value_text must be the exact printable value text')
    if type(aa['value_text_pinned']) is not bool:
        errors.append('requirements.antialiasing.value_text_pinned must be true or false')
    others = aa['other_mode_texts']
    if (not isinstance(others, dict) or any(k not in AA_MODES or k == aa['mode'] or not text(v)
                                            for k, v in others.items())
            or aa['value_text'] in others.values()):
        errors.append('requirements.antialiasing.other_mode_texts must map other modes to observed texts')
    if not isinstance(aa['evidence'], str) or len(aa['evidence']) < 20:
        errors.append('requirements.antialiasing.evidence must say where the value text comes from')
    return errors


def image_check_errors(c, crops):
    errors = _keys(c, IMAGE_CHECKS, ('notes',), 'image_checks')
    if errors:
        return errors
    if (not isinstance(c['crops'], list) or not c['crops'] or len(set(c['crops'])) != len(c['crops'])
            or any(x not in crops for x in c['crops'])):
        errors.append('image_checks.crops must list known crops without duplicates')
    if (not isinstance(c['stats'], list) or not c['stats'] or len(set(c['stats'])) != len(c['stats'])
            or any(x not in STATS for x in c['stats'])):
        errors.append('image_checks.stats must list known statistics without duplicates')
    if not _number(c['max_stat_difference']) or not 0 <= c['max_stat_difference'] <= 255:
        errors.append('image_checks.max_stat_difference must be a number from 0 to 255')
    if type(c['pixel_threshold']) is not int or not 0 <= c['pixel_threshold'] <= 255:
        errors.append('image_checks.pixel_threshold must be an integer from 0 to 255')
    if type(c['max_pixels_over_threshold']) is not int or c['max_pixels_over_threshold'] < 0:
        errors.append('image_checks.max_pixels_over_threshold must be an integer >= 0')
    return errors


def layout_errors(parts):
    """Interface and free-camera state across the typed parts (mirrors check_layout in C++).

    Every Screenshot must find the interface hidden (an odd number of `tm` so far)
    and the free camera on; the last part must leave both as at the start. Part
    files holding Screenshot hold nothing else but the mark (check_bat), so no
    toggle can share a file with a photograph in either order.
    """
    hidden = free = False
    for x in parts:
        for c in x['commands']:
            verb = c.split(' ')[0].lower()
            if verb == 'tm':
                hidden = not hidden
            elif verb == 'tfc':
                free = not free
            elif verb == 'screenshot' and not (hidden and free):
                return ['part %s: Screenshot while the interface is shown or the free camera is off' % x['name']]
    if hidden or free:
        return ['the parts leave the interface hidden or the free camera on']
    return []


def validate_packet(p, retired_ok=False):
    """Return every error found; an empty list means the packet is valid.

    retired_ok accepts a retired template, for analysing runs already made
    with it; such a packet can never be compiled or built.
    """
    errors = _keys(p, TOP, TOP_OPTIONAL)
    if errors:
        return errors
    if p['packet_schema'] != PACKET_SCHEMA:
        errors.append('packet_schema must be 1')
    if not isinstance(p['id'], str) or not ID_RE.fullmatch(p['id']):
        errors.append('id must match ' + ID_RE.pattern)
    if type(p['version']) is not int or p['version'] < 1:
        errors.append('version must be a positive integer')
    if not isinstance(p['rule'], str) or not RULE_RE.fullmatch(p['rule']):
        errors.append('rule must match ' + RULE_RE.pattern)
    if p['rule_status_before'] not in ('pending', 'supported', 'refuted'):
        errors.append('rule_status_before must be pending, supported or refuted')
    q = p['question']
    if not isinstance(q, str) or not 10 <= len(q) <= 400 or '\n' in q or not q.endswith('?'):
        errors.append('question must be one sentence ending in ?')
    if type(p['estimated_minutes']) is not int or not 1 <= p['estimated_minutes'] <= 600:
        errors.append('estimated_minutes must be an integer from 1 to 600')
    r = p['requirements']
    e = _keys(r, REQUIREMENTS, REQUIREMENTS_OPTIONAL, 'requirements')
    errors += e
    width = height = 0
    if not e:
        if r['game_version'] != '1.10.163.0' or r['f4se_version'] != '0.6.23' or r['f4se_release_index'] != 25:
            errors.append('requirements support only Fallout 4 1.10.163.0 and F4SE 0.6.23 release 25')
        if type(r['plugin_min_version']) is not int or r['plugin_min_version'] < 9:
            errors.append('plugin_min_version must be at least 9')
        res = r['resolution']
        if not (isinstance(res, list) and len(res) == 2 and all(type(v) is int and 64 <= v <= 16384 for v in res)):
            errors.append('resolution must be [width, height]')
        else:
            width, height = res
        errors += antialiasing_errors(r['antialiasing'], r['ini_keys'])
        if 'executable' in r:
            x = r['executable']
            if (_keys(x, ('file', 'size', 'sha256'), (), 'requirements.executable') or x['file'] != 'Fallout4.exe'
                    or type(x['size']) is not int or not 0 < x['size'] <= 1024 ** 3
                    or not isinstance(x['sha256'], str) or not SHA_RE.fullmatch(x['sha256'])):
                errors.append('requirements.executable must be {file: Fallout4.exe, size, sha256}')
        if r['data_unmodded'] is not True:
            errors.append('data_unmodded must be true')
        plugins = r['active_plugins']
        if not isinstance(plugins, list) or not 2 <= len(plugins) <= 254:
            errors.append('active_plugins must list the exact load order')
        else:
            for i, m in enumerate(plugins):
                if (_keys(m, ('name', 'kind'), (), 'plugin') or not FILE_RE.fullmatch(str(m['name']))
                        or m['kind'] not in ('full', 'light')
                        or not str(m['name']).lower().endswith(('.esm', '.esp', '.esl'))):
                    errors.append('active_plugins[%d] must be {name, kind}' % i)
            names = [str(m.get('name', '')).lower() for m in plugins if isinstance(m, dict)]
            if len(set(names)) != len(names):
                errors.append('active_plugins has duplicates')
        for key in ('hashed_files', 'ini_keys'):
            rows = r[key]
            if (not isinstance(rows, list) or not all(isinstance(x, str) for x in rows)
                    or len({x.lower() for x in rows}) != len(rows)):
                errors.append(key + ' must be a list of names without duplicates')
        if isinstance(r['hashed_files'], list):
            for f in r['hashed_files']:
                if not isinstance(f, str) or not FILE_RE.fullmatch(f):
                    errors.append('hashed_files entry is not a plain file name: %r' % (f,))
        if isinstance(r['ini_keys'], list):
            for k in r['ini_keys']:
                if not isinstance(k, str) or not INI_KEY_RE.fullmatch(k):
                    errors.append('ini key must look like name:Section or name: %r' % (k,))
    b = p['bench_plugin']
    e = _keys(b, ('file', 'size', 'sha256'), (), 'bench_plugin')
    errors += e
    if not e:
        if not isinstance(b['file'], str) or not FILE_RE.fullmatch(b['file']) or not b['file'].lower().endswith('.esp'):
            errors.append('bench_plugin.file must be a plain .esp name inside Data')
        if type(b['size']) is not int or not 0 < b['size'] <= 64 * 1024 * 1024:
            errors.append('bench_plugin.size must be a positive integer')
        if not isinstance(b['sha256'], str) or not SHA_RE.fullmatch(b['sha256']):
            errors.append('bench_plugin.sha256 must be 64 lowercase hex digits')
        plugins = r.get('active_plugins') if isinstance(r, dict) else None
        if isinstance(plugins, list) and plugins and isinstance(plugins[-1], dict):
            if str(plugins[-1].get('name', '')).lower() != str(b['file']).lower():
                errors.append('the bench plugin must be the last active plugin')
    c = p['cell']
    e = _keys(c, ('editor_id', 'local_form_id'), (), 'cell')
    errors += e
    if not e and (not EDITOR_ID_RE.fullmatch(str(c['editor_id'])) or not LOCAL_ID_RE.fullmatch(str(c['local_form_id']))):
        errors.append('cell needs an EditorID and a six-digit uppercase local FormID')
    if not isinstance(p['bat_prefix'], str) or not PREFIX_RE.fullmatch(p['bat_prefix']):
        errors.append('bat_prefix must be 1-8 lowercase letters')
    if p['template'] in RETIRED_TEMPLATES:
        if not retired_ok:
            errors.append('template %s is retired: its repeat part shares a file between tm and Screenshot; '
                          'it can only be analysed, not built' % p['template'])
    elif p['template'] != TEMPLATE:
        errors.append('template must be ' + TEMPLATE)
    else:
        if isinstance(r, dict) and 'executable' not in r:
            errors.append('requirements.executable is required from %s on' % TEMPLATE)
        if isinstance(r, dict) and isinstance(r.get('antialiasing'), str):
            errors.append('requirements.antialiasing must name its value text from %s on' % TEMPLATE)
        if 'image_checks' not in p:
            errors.append('image_checks is required from %s on' % TEMPLATE)
    if not isinstance(p['screenshot_base_name'], str) or not re.fullmatch(r'[A-Za-z0-9_]{1,32}', p['screenshot_base_name']):
        errors.append('screenshot_base_name must be a plain name')
    tol = p['position_tolerance']
    if not _number(tol) or not 0 < tol <= 1:
        errors.append('position_tolerance must be in (0, 1]')
    steps = {}
    rows = p['steps']
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_STEPS:
        errors.append('steps must hold 1 to %d steps' % MAX_STEPS)
        rows = []
    for i, s in enumerate(rows):
        w = 'steps[%d]' % i
        e = _keys(s, STEP, STEP_OPTIONAL, w)
        if e:
            errors += e
            continue
        if not isinstance(s['id'], str) or not STEP_RE.fullmatch(s['id']) or s['id'] in steps:
            errors.append(w + '.id must be a unique ' + STEP_RE.pattern)
            continue
        steps[s['id']] = s
        if s['kind'] not in ('measure', 'canary'):
            errors.append(w + '.kind must be measure or canary')
        pos, ang = s['position'], s['angles']
        if not (isinstance(pos, list) and len(pos) == 3 and all(_number(v) for v in pos)):
            errors.append(w + '.position must be three finite numbers')
        else:
            for v in pos:
                try:
                    decimal_text(v)
                except PacketError as ex:
                    errors.append(w + ': ' + str(ex))
        if not (isinstance(ang, list) and len(ang) == 3 and all(type(v) is int and -360 <= v <= 360 for v in ang)):
            errors.append(w + '.angles must be three integers in [-360, 360]')
        if type(s['settle_seconds']) is not int or not 1 <= s['settle_seconds'] <= 600:
            errors.append(w + '.settle_seconds must be an integer from 1 to 600')
        if s['simulated_time'] not in ('require', 'record'):
            errors.append(w + '.simulated_time must be require or record')
        if type(s['free_camera_seconds']) is not int or not 0 <= s['free_camera_seconds'] <= 60:
            errors.append(w + '.free_camera_seconds must be an integer from 0 to 60')
        roots = s['roots']
        if not isinstance(roots, list) or len(roots) > 8 or not all(isinstance(x, str) and LOCAL_ID_RE.fullmatch(x) for x in roots):
            errors.append(w + '.roots must list at most eight six-digit bench-local FormIDs')
    if p['mask'] not in ('corners32', 'none'):
        errors.append('mask must be corners32 or none')
    crops = p['crops']
    if not isinstance(crops, dict) or not crops:
        errors.append('crops must be a non-empty object')
        crops = {}
    for name, rects in crops.items():
        if not STEP_RE.fullmatch(str(name)):
            errors.append('crop name invalid: %r' % (name,))
        if (not isinstance(rects, list) or not 1 <= len(rects) <= 8
                or not all(_rect_ok(x, width or 16384, height or 16384) for x in rects)):
            errors.append('crop %s must be 1-8 integer [x0,y0,x1,y1) rectangles inside the frame' % name)
    if 'image_checks' in p:
        errors += image_check_errors(p['image_checks'], crops)
    measure_steps = {k for k, s in steps.items() if s['kind'] == 'measure'}
    canary_steps = {k for k, s in steps.items() if s['kind'] == 'canary'}
    errors += _criterion_errors(p['criterion'], measure_steps, crops, 'criterion')
    canaries = p['canaries']
    if not isinstance(canaries, list):
        errors.append('canaries must be a list')
        canaries = []
    used = set()
    for i, k in enumerate(canaries):
        w = 'canaries[%d]' % i
        e = _keys(k, ('rule', 'expected', 'criterion'), ('notes',), w)
        if e:
            errors += e
            continue
        if not RULE_RE.fullmatch(str(k['rule'])) or k['rule'] == p['rule']:
            errors.append(w + '.rule must name another, already decided rule')
        if k['expected'] not in ('supported', 'refuted'):
            errors.append(w + '.expected must be supported or refuted')
        errors += _criterion_errors(k['criterion'], canary_steps, crops, w + '.criterion')
        if isinstance(k['criterion'], dict) and isinstance(k['criterion'].get('measures'), dict):
            used |= {m.get('step') for m in k['criterion']['measures'].values() if isinstance(m, dict)}
    if canary_steps - used:
        errors.append('canary steps not used by any canary: ' + ', '.join(sorted(canary_steps - used)))
    if not canaries:
        ex = p.get('canary_exemption')
        if not isinstance(ex, str) or len(ex) < 20:
            errors.append('a packet without canaries must state canary_exemption')
    elif 'canary_exemption' in p:
        errors.append('canary_exemption is only for packets without canaries')
    out = p['expected_outputs']
    e = _keys(out, ('parts', 'marks', 'screenshots', 'steps'), (), 'expected_outputs')
    errors += e
    if not errors:
        parts = expand(p)
        want = dict(parts=len(parts), marks=len(parts), screenshots=sum(x['shots'] for x in parts),
                    steps=len(steps))
        if out != want:
            errors.append('expected_outputs must be %s' % json.dumps(want, sort_keys=True))
        if p['template'] == TEMPLATE:
            for x in parts:
                try:
                    check_bat(bat_bytes(x), cell=p['cell']['editor_id'])
                except PacketError as ex:
                    errors.append('part %s: %s' % (x['name'], ex))
            errors += layout_errors(parts)
    return errors


def expand(p):
    """Expand the packet's template into the exact typed parts, in order.

    photo_v2 (docs/packet.md): the mark is the last line of every part except the
    cell load, so the plugin's next-command line is the last thing a part prints.
    The capture part hides the interface; photo and repeat each hold one
    Screenshot and the mark only; restore shows the interface and leaves the
    free camera. photo_v1 is kept only to describe runs already made with it.
    """
    if p['template'] == 'photo_v1':
        return _expand_photo_v1(p)
    prefix, cell = p['bat_prefix'], p['cell']['editor_id']
    parts = [dict(name=prefix + '00a', role='start', step='-', commands=[MARK]),
             dict(name=prefix + '00b', role='note', step='-', commands=[MARK, 'coc ' + cell]),
             dict(name=prefix + '00c', role='cell', step='-', commands=['tcl', MARK])]
    for n, s in enumerate(p['steps'], 1):
        tag = prefix + '%02d' % n
        pose = ['player.setpos %s %s' % (a, decimal_text(v)) for a, v in zip('xyz', s['position'])]
        pose += ['player.setangle %s %d' % (a, v) for a, v in zip('xyz', s['angles'])]
        parts += [dict(name=tag + 'a', role='pose', step=s['id'], commands=pose + [MARK]),
                  dict(name=tag + 'b', role='settle', step=s['id'], commands=[MARK]),
                  dict(name=tag + 'c', role='free', step=s['id'], commands=['tfc 1', MARK]),
                  dict(name=tag + 'd', role='capture', step=s['id'], commands=['tm', MARK]),
                  dict(name=tag + 'e', role='photo', step=s['id'], commands=['Screenshot', MARK]),
                  dict(name=tag + 'f', role='repeat', step=s['id'], commands=['Screenshot', MARK]),
                  dict(name=tag + 'g', role='restore', step=s['id'], commands=['tm', 'tfc 1', MARK])]
    parts.append(dict(name=prefix + '99', role='end', step='-', commands=['tcl', MARK]))
    for x in parts:
        x['shots'] = sum(c.lower() == 'screenshot' for c in x['commands'])
    return parts


def _expand_photo_v1(p):
    """The retired layout of packet versions 1 and 2, exactly as round 25 typed it."""
    prefix, cell = p['bat_prefix'], p['cell']['editor_id']
    parts = [dict(name=prefix + '00a', role='start', step='-', commands=[MARK]),
             dict(name=prefix + '00b', role='note', step='-', commands=[MARK, 'coc ' + cell]),
             dict(name=prefix + '00c', role='cell', step='-', commands=[MARK, 'tcl'])]
    for n, s in enumerate(p['steps'], 1):
        tag = prefix + '%02d' % n
        pose = ['player.setpos %s %s' % (a, decimal_text(v)) for a, v in zip('xyz', s['position'])]
        pose += ['player.setangle %s %d' % (a, v) for a, v in zip('xyz', s['angles'])]
        parts += [dict(name=tag + 'a', role='pose', step=s['id'], commands=pose + [MARK]),
                  dict(name=tag + 'b', role='settle', step=s['id'], commands=[MARK]),
                  dict(name=tag + 'c', role='free', step=s['id'], commands=[MARK, 'tfc 1']),
                  dict(name=tag + 'd', role='capture', step=s['id'], commands=[MARK, 'tm', 'Screenshot']),
                  dict(name=tag + 'e', role='repeat', step=s['id'], commands=['Screenshot', 'tm', MARK, 'tfc 1'])]
    parts.append(dict(name=prefix + '99', role='end', step='-', commands=['tcl', MARK]))
    for x in parts:
        x['shots'] = sum(c.lower() == 'screenshot' for c in x['commands'])
    return parts


def bat_bytes(part):
    return ''.join(c + '\r\n' for c in part['commands']).encode('ascii')


def check_bat(data, cell):
    """The same checks the plugin applies to a part file before the first step."""
    if len(data) > 4096:
        raise PacketError('part file larger than 4096 bytes')
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        raise PacketError('part file must be ASCII')
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    lines = [x[:-1] if x.endswith('\r') else x for x in lines]
    if not lines or len(lines) > MAX_BAT_LINES:
        raise PacketError('part file must hold 1 to %d lines' % MAX_BAT_LINES)
    verbs = [validate_command(x, cell)[0] for x in lines]
    if verbs.count(MARK) != 1:
        raise PacketError('part file must contain exactly one ' + MARK)
    shots = verbs.count('screenshot')
    # The game takes the picture after the whole file has run (round 25), so any
    # interface, camera or pose command in the same file would act on it.
    if shots > 1:
        raise PacketError('a part file may request at most one Screenshot')
    if shots and any(v not in (MARK, 'screenshot') for v in verbs):
        raise PacketError('a part file with Screenshot may hold only Screenshot and ' + MARK)
    return dict(lines=lines, shots=shots)


def compile_plan(p, packet_sha256):
    """Line-based plan read by the plugin; every number is exact text or a word."""
    errors = validate_packet(p)
    if errors:
        raise PacketError('invalid packet: ' + '; '.join(errors))
    tol = float32_word(p['position_tolerance'])
    lines = ['osp-plan %d' % PLAN_FORMAT,
             'packet %s %d %s' % (p['id'], p['version'], packet_sha256),
             'bench %s %d %s' % (p['bench_plugin']['file'].replace(' ', '|'), p['bench_plugin']['size'],
                                 p['bench_plugin']['sha256']),
             'cell %s %s' % (p['cell']['editor_id'], p['cell']['local_form_id']),
             'screenshot %s' % p['screenshot_base_name'],
             'exe %d %s' % (p['requirements']['executable']['size'], p['requirements']['executable']['sha256'])]
    for m in p['requirements']['active_plugins']:
        lines.append('plugin %s %s' % (m['kind'], m['name'].replace(' ', '|')))
    for k in p['requirements']['ini_keys']:
        lines.append('ini ' + k.replace(' ', '|'))
    steps = {s['id']: s for s in p['steps']}
    for x in expand(p):
        row = 'part %s %s %s %s %d' % (x['name'], hashlib.sha256(bat_bytes(x)).hexdigest(),
                                       x['role'], x['step'], x['shots'])
        s = steps.get(x['step'])
        if x['role'] == 'settle':
            row += ' %d %s %s' % (s['settle_seconds'], s['simulated_time'],
                                  ' '.join(float32_word(v) for v in s['position']) + ' ' + tol)
        elif x['role'] == 'capture':
            row += ' %d %d' % (s['free_camera_seconds'], len(s['roots']))
            if s['roots']:
                row += ' ' + ' '.join(s['roots'])
        lines.append(row)
    lines.append('end %d' % len(expand(p)))
    return ''.join(x + '\n' for x in lines)


def load(path):
    data = Path(path).read_bytes()
    try:
        packet = json.loads(data.decode('utf-8'))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise PacketError('packet.json is not UTF-8 JSON: %s' % e)
    return packet, hashlib.sha256(data).hexdigest()


def build_install(packet_path, out_dir, bench=None):
    """Write the files a person copies into the game: never into the game itself.

    Layout under out_dir: `game-root/<part>.txt` (bat files for the game folder)
    and `game-root/Data/F4SE/Plugins/osp_measure/packet/{packet.json,plan.txt}`.
    With --bench, the bench plugin is copied to game-root/Data after its size
    and SHA-256 match the packet.
    """
    packet, digest = load(packet_path)
    errors = validate_packet(packet)
    if errors:
        raise PacketError('; '.join(errors))
    out = Path(out_dir)
    if out.exists() and any(out.iterdir()):
        raise PacketError('output folder must be new or empty')
    root = out / 'game-root'
    folder = root / 'Data' / 'F4SE' / 'Plugins' / 'osp_measure' / 'packet'
    folder.mkdir(parents=True)
    (folder / 'packet.json').write_bytes(Path(packet_path).read_bytes())
    (folder / 'plan.txt').write_bytes(compile_plan(packet, digest).encode('ascii'))
    for x in expand(packet):
        (root / (x['name'] + '.txt')).write_bytes(bat_bytes(x))
    if bench is not None:
        data = Path(bench).read_bytes()
        b = packet['bench_plugin']
        if len(data) != b['size'] or hashlib.sha256(data).hexdigest() != b['sha256']:
            raise PacketError('bench plugin size or SHA-256 differs from the packet')
        (root / 'Data' / b['file']).write_bytes(data)
    return dict(packet_sha256=digest, parts=len(expand(packet)), root=str(root))
