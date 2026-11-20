# SPDX-License-Identifier: MIT OR Apache-2.0
"""Packet format: closed command list, validator, expansion, plan and install files."""
import copy
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packet_fixtures as fx  # noqa: E402
import packet_format as pf  # noqa: E402


class Commands(unittest.TestCase):
    def test_closed_list_accepts_only_allowed_forms(self):
        cell = 'OSPBenchCell02'
        for line in ['player.setpos x 8192', 'player.setpos z -113.310157776', 'Player.SetPos y 1.5',
                     'player.setangle z 180', 'player.setangle x -360', 'tfc', 'tfc 1', 'tm', 'TCL',
                     'Screenshot', 'coc OSPBenchCell02', 'ospsnap']:
            pf.validate_command(line, cell)
        for line in ['save quick', 'qqq', 'player.setav health 1', 'player.setpos x 1e3', 'player.setpos w 1',
                     'player.setangle z 1.5', 'player.setangle z 361', 'tfc 2', 'tm 1', 'coc Sanctuary',
                     'tcl;save x', 'bat other', 'ospsnap tag', ' tm', 'tm ', 'player.setpos  x 1',
                     'prid 14', 'ospnodes', 'screenshot "a"', 'player.setpos x 12345678',
                     'player.setpos x 1.1234567891', '']:
            with self.assertRaises(pf.PacketError, msg=line):
                pf.validate_command(line, cell)
        with self.assertRaises(pf.PacketError):
            pf.validate_command('coc OSPBenchCell02', None)

    def test_part_file_checks(self):
        cell = 'OSPBenchCell02'
        self.assertEqual(pf.check_bat(b'Screenshot\r\nospsnap\r\n', cell)['shots'], 1)
        self.assertEqual(pf.check_bat(b'tm\r\ntfc 1\r\nospsnap\r\n', cell)['shots'], 0)
        for data in [b'tm\r\n', b'ospsnap\r\nospsnap\r\n', b'ospsnap\r\nsave x\r\n',
                     b'ospsnap\r\ncoc Other\r\n', 'ospsnap\r\ntm\xe9\r\n'.encode('latin-1'),
                     b'ospsnap\r\n' + b'tm\r\n' * 16, b'x' * 5000]:
            with self.assertRaises(pf.PacketError, msg=data[:30]):
                pf.check_bat(data, cell)

    def test_screenshot_never_shares_a_file_with_a_toggle(self):
        # Round 25: the game takes the picture after the whole file has run, so
        # both orders of tm and Screenshot in one file are refused, as is any
        # other command that changes the image, and two pictures in one file.
        cell = 'OSPBenchCell02'
        for data in [b'ospsnap\r\ntm\r\nScreenshot\r\n', b'Screenshot\r\ntm\r\nospsnap\r\ntfc 1\r\n',
                     b'Screenshot\r\ntfc 1\r\nospsnap\r\n', b'player.setangle z 0\r\nScreenshot\r\nospsnap\r\n',
                     b'tcl\r\nScreenshot\r\nospsnap\r\n', b'Screenshot\r\nScreenshot\r\nospsnap\r\n']:
            with self.assertRaises(pf.PacketError, msg=data):
                pf.check_bat(data, cell)

    def test_layout_simulation(self):
        part = lambda name, *c: dict(name=name, commands=list(c))
        good = [part('a', 'tfc 1', 'ospsnap'), part('b', 'tm', 'ospsnap'), part('c', 'Screenshot', 'ospsnap'),
                part('d', 'tm', 'tfc 1', 'ospsnap')]
        self.assertEqual(pf.layout_errors(good), [])
        shown = [good[0], part('c', 'Screenshot', 'ospsnap'), good[1], good[3]]
        self.assertIn('Screenshot while the interface is shown', pf.layout_errors(shown)[0])
        no_free = [good[1], part('c', 'Screenshot', 'ospsnap'), part('d', 'tm', 'ospsnap')]
        self.assertIn('free camera is off', pf.layout_errors(no_free)[0])
        self.assertIn('leave the interface hidden', pf.layout_errors(good[:3])[0])


class Packets(unittest.TestCase):
    def setUp(self):
        self.p = fx.canary_packet()

    def assertInvalid(self, p, fragment):
        errors = pf.validate_packet(p)
        self.assertTrue(any(fragment in e for e in errors), errors)

    def test_canary_packet_is_valid_and_expands(self):
        self.assertEqual(pf.validate_packet(self.p), [])
        parts = pf.expand(self.p)
        self.assertEqual(len(parts), 32)
        self.assertEqual([x['name'] for x in parts[:4]], ['cull00a', 'cull00b', 'cull00c', 'cull01a'])
        self.assertEqual(sum(x['shots'] for x in parts), 8)
        for x in parts:
            pf.check_bat(pf.bat_bytes(x), 'OSPBenchCell02')
        self.assertEqual(parts[3]['commands'][1], 'player.setpos y 10200.000091553')
        step = [(x['name'], x['role'], x['commands']) for x in parts[3:10]]
        self.assertEqual(step[3:], [('cull01d', 'capture', ['tm', 'ospsnap']),
                                    ('cull01e', 'photo', ['Screenshot', 'ospsnap']),
                                    ('cull01f', 'repeat', ['Screenshot', 'ospsnap']),
                                    ('cull01g', 'restore', ['tm', 'tfc 1', 'ospsnap'])])
        # The mark is the last line everywhere except the cell load, so the next-command
        # line is the last thing a part prints.
        for x in parts:
            if x['role'] != 'note':
                self.assertEqual(x['commands'][-1], 'ospsnap', x['name'])

    def test_measured_version_2_is_kept_and_retired(self):
        v2 = fx.v2_packet()
        self.assertEqual(v2['version'], 2)
        import hashlib
        self.assertEqual(hashlib.sha256(fx.V2_PACKET.read_bytes()).hexdigest(),
                         '0912933192d933c7b38f598dbfe2906107d98da9ee7facdc7ddeedbc413bf94a')
        self.assertTrue(any('retired' in e for e in pf.validate_packet(v2)))
        self.assertEqual(pf.validate_packet(v2, retired_ok=True), [])
        self.assertEqual(len(pf.expand(v2)), 24)          # still describes round 25 exactly
        with self.assertRaises(pf.PacketError):
            pf.compile_plan(v2, 'a' * 64)
        with tempfile.TemporaryDirectory() as t:
            with self.assertRaises(pf.PacketError):
                pf.build_install(fx.V2_PACKET, Path(t) / 'install')
        # Version 3 differs from version 2 only in how the run is made and checked.
        v3 = fx.v3_packet()
        same = ('criterion', 'canaries', 'steps', 'bench_plugin', 'cell', 'mask', 'position_tolerance', 'rule',
                'question', 'canary_exemption', 'bat_prefix', 'screenshot_base_name')
        for key in same:
            self.assertEqual(v2[key], v3[key], key)
        self.assertEqual({k: v for k, v in v3['crops'].items() if k != 'overlay_watch'}, v2['crops'])

    def test_version_4_differs_from_version_3_only_in_settings_and_antialiasing(self):
        import hashlib
        sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        # Version 3 was measured in round 26 and stays byte for byte; version 4 is pinned as published.
        self.assertEqual(sha(fx.V3_PACKET), '294913c58e16744607d5ecb0e982b0fbbc8841b3457e1bf07bffa7238ee94e37')
        self.assertEqual(sha(fx.V4_PACKET), 'ef4c03eaa7d860fb7b49b718f2be9767ba35eed5425308fc2e835f7563bfe7c4')
        v3, v4 = fx.v3_packet(), fx.canary_packet()
        self.assertEqual(pf.validate_packet(v3), [])
        self.assertEqual(pf.validate_packet(v4), [])
        self.assertEqual(set(v3), set(v4))
        self.assertEqual({k for k in v3 if v3[k] != v4[k]}, {'version', 'requirements', 'notes'})
        self.assertEqual((v3['version'], v4['version']), (3, 4))
        self.assertEqual(v4['notes'][:len(v3['notes'])], v3['notes'])   # notes are only appended
        r3, r4 = v3['requirements'], v4['requirements']
        self.assertEqual({k for k in set(r3) | set(r4) if r3.get(k) != r4.get(k)}, {'ini_keys', 'antialiasing'})
        a3, a4 = r3['antialiasing'], r4['antialiasing']
        self.assertEqual({k for k in set(a3) | set(a4) if a3.get(k) != a4.get(k)},
                         {'key', 'value_text_pinned', 'evidence'})
        self.assertEqual((a4['key'], a4['mode'], a4['value_text'], a4['value_text_pinned']),
                         ('SAntiAliasing:Display', 'FXAA', 'FXAA', True))
        # Round 26 found eleven of version 3's names exactly; version 4 keeps them, drops the other
        # ten and adds the six names that run recorded as the only settings near them.
        found = ['iSize W:Display', 'iSize H:Display', 'bFull Screen:Display', 'iPresentInterval:Display',
                 'fDefaultWorldFOV:Display', 'fDefault1stPersonFOV:Display', 'iScreenShotIndex:Display',
                 'fAutosaveEveryXMins:SaveGame', 'bDisableAutoSave:SaveGame', 'sResourceDataDirsFinal:Archive',
                 'bInvalidateOlderFiles:Archive']
        self.assertEqual([k for k in r4['ini_keys'] if k in r3['ini_keys']], found)
        self.assertEqual(set(r3['ini_keys']) - set(r4['ini_keys']), {
            'sAntiAliasing:Display', 'SScreenShotBaseName:Display', 'bAllowScreenShot:Display',
            'bSaveOnTravel:SaveGame', 'bSaveOnWait:SaveGame', 'bSaveOnRest:SaveGame', 'bSaveOnPause:MAIN',
            'bSaveOnTravel:MAIN', 'bSaveOnWait:MAIN', 'bSaveOnRest:MAIN'})
        self.assertEqual([k for k in r4['ini_keys'] if k not in r3['ini_keys']], [
            'SAntiAliasing:Display', 'sScreenShotBaseName:Display', 'bSaveOnTravel', 'bSaveOnWait', 'bSaveOnRest',
            'bSaveOnPause'])
        # The typed parts are identical; the plans differ only in the packet line and the setting names.
        self.assertEqual([(x['name'], pf.bat_bytes(x)) for x in pf.expand(v3)],
                         [(x['name'], pf.bat_bytes(x)) for x in pf.expand(v4)])
        rest = lambda p: [x for x in pf.compile_plan(p, 'a' * 64).splitlines()
                          if not x.startswith(('packet ', 'ini '))]
        self.assertEqual(rest(v3), rest(v4))

    def test_photo_v2_requires_its_new_fields(self):
        for change, fragment in [(lambda p: p['requirements'].pop('executable'), 'executable is required'),
                                 (lambda p: p['requirements'].update(antialiasing='FXAA'), 'name its value text'),
                                 (lambda p: p.pop('image_checks'), 'image_checks is required'),
                                 (lambda p: p['image_checks'].update(crops=['nope']), 'image_checks.crops'),
                                 (lambda p: p['image_checks'].update(stats=['mean_x']), 'image_checks.stats'),
                                 (lambda p: p['requirements']['executable'].update(sha256='X'), 'requirements.executable'),
                                 (lambda p: p['requirements']['antialiasing'].update(other_mode_texts={'TAA': 'FXAA'}),
                                  'other_mode_texts'),
                                 (lambda p: p['requirements']['antialiasing'].update(value_text_pinned='no'), 'pinned')]:
            p = copy.deepcopy(self.p)
            change(p)
            self.assertInvalid(p, fragment)

    def test_plan_is_deterministic_and_pins_words(self):
        plan = pf.compile_plan(self.p, 'a' * 64)
        self.assertEqual(plan, pf.compile_plan(copy.deepcopy(self.p), 'a' * 64))
        self.assertIn('part cull01b ', plan)
        settle = [x for x in plan.splitlines() if ' settle sign044_front ' in x][0]
        self.assertTrue(settle.endswith('40 require 46000000 461F6000 C2E29ECD 3C23D70A'))
        self.assertIn('ini iSize|W:Display\n', plan)
        self.assertTrue(plan.startswith('osp-plan 2\n'))
        self.assertIn('\nexe 65503104 55f57947db9e05575122fae1088f0b0247442f11e566b56036caa0ac93329c36\n', plan)
        self.assertTrue(plan.endswith('end 32\n'))

    def test_build_writes_only_into_new_folder(self):
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'install'
            info = pf.build_install(fx.CANARY_PACKET, out)
            self.assertEqual(info['parts'], 32)
            names = sorted(p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file())
            self.assertIn('game-root/Data/F4SE/Plugins/osp_measure/packet/plan.txt', names)
            self.assertIn('game-root/cull99.txt', names)
            self.assertIn('game-root/cull04g.txt', names)
            self.assertEqual(len(names), 34)
            with self.assertRaises(pf.PacketError):
                pf.build_install(fx.CANARY_PACKET, out)
            bad = Path(t) / 'fake.esp'
            bad.write_bytes(b'not the bench')
            with self.assertRaises(pf.PacketError):
                pf.build_install(fx.CANARY_PACKET, Path(t) / 'other', bad)

    def test_paths_that_escape_are_refused(self):
        for field, value in [('file', '..\\Fallout4.esm'), ('file', '../x.esp'), ('file', 'C:\\x.esp'),
                             ('file', 'sub/x.esp'), ('file', 'x.dll')]:
            p = copy.deepcopy(self.p)
            p['bench_plugin'][field] = value
            self.assertInvalid(p, 'bench_plugin.file')
        p = copy.deepcopy(self.p)
        p['requirements']['hashed_files'].append('..\\..\\Windows\\win.ini')
        self.assertInvalid(p, 'hashed_files entry')
        p = copy.deepcopy(self.p)
        p['bat_prefix'] = '../a'
        self.assertInvalid(p, 'bat_prefix')
        p = copy.deepcopy(self.p)
        p['id'] = '../../escape'
        self.assertInvalid(p, 'id must match')
        p = copy.deepcopy(self.p)
        p['steps'][0]['id'] = 'a/b'
        self.assertInvalid(p, 'steps[0].id')

    def test_bad_packets(self):
        cases = [
            (lambda p: p.update(packet_schema=2), 'packet_schema'),
            (lambda p: p.update(extra=1), 'unknown fields'),
            (lambda p: p['requirements'].update(game_version='1.10.984.0'), 'support only'),
            (lambda p: p['requirements']['active_plugins'].reverse(), 'last active plugin'),
            (lambda p: p['steps'][0].update(angles=[0, 0, 1.5]), 'angles'),
            (lambda p: p['steps'][0].update(position=[1e9, 0, 0]), 'decimal form'),
            (lambda p: p['steps'][0].update(simulated_time='maybe'), 'simulated_time'),
            (lambda p: p['crops'].update(sign_band=[[0, 0, 1281, 10]]), 'crop sign_band'),
            (lambda p: p['criterion']['measures']['front_044'].update(step='nope'), 'unknown step'),
            (lambda p: p['criterion']['measures']['front_044'].update(control='sign_band'), 'itself'),
            (lambda p: p['criterion'].update(supported_if_all=[]), 'supported_if_all'),
            (lambda p: p['criterion']['refuted_if_any'][0].update(ref='front_044_missing'), 'ref'),
            (lambda p: p['criterion']['controls'][0].update(op='=='), 'operator'),
            (lambda p: p.pop('canary_exemption'), 'canary_exemption'),
            (lambda p: p['expected_outputs'].update(screenshots=9), 'expected_outputs'),
            (lambda p: p.update(question='No question mark.'), 'question'),
            (lambda p: p['requirements'].update(data_unmodded=False), 'data_unmodded'),
        ]
        for change, fragment in cases:
            p = copy.deepcopy(self.p)
            change(p)
            self.assertInvalid(p, fragment)

    def test_setting_names_with_or_without_a_section(self):
        # Round 26 found the autosave settings under names without a section.
        p = copy.deepcopy(self.p)
        p['requirements']['ini_keys'] += ['SyntheticName', 'Key With Spaces']
        self.assertEqual(pf.validate_packet(p), [])
        self.assertIn('ini SyntheticName\nini Key|With|Spaces\n', pf.compile_plan(p, 'a' * 64))
        for name in ['a:b:c', ':Display', 'SyntheticName:', 'Key-x:Display', 'Key:Dis.play', '1Key']:
            q = copy.deepcopy(self.p)
            q['requirements']['ini_keys'].append(name)
            self.assertInvalid(q, 'ini key must look like')

    def test_canary_validation(self):
        p = fx.with_canary(self.p)
        self.assertEqual(pf.validate_packet(p), [])
        q = copy.deepcopy(p)
        q['canaries'][0]['rule'] = q['rule']
        self.assertInvalid(q, 'another, already decided rule')
        q = copy.deepcopy(p)
        q['canary_exemption'] = 'x' * 30
        self.assertInvalid(q, 'only for packets without canaries')
        q = copy.deepcopy(p)
        q['canaries'] = []
        q['canary_exemption'] = 'x' * 30
        self.assertInvalid(q, 'canary steps not used')

class Schema(unittest.TestCase):
    def test_structural_schema_matches_validator_fields(self):
        import json as _json
        schema = _json.loads((fx.ROOT / 'schemas' / 'packet.schema.json').read_text())
        self.assertEqual(set(schema['required']), set(pf.TOP))
        self.assertEqual(set(schema['properties']), set(pf.TOP) | set(pf.TOP_OPTIONAL))
        req = schema['properties']['requirements']
        self.assertEqual(set(req['required']), set(pf.REQUIREMENTS))
        self.assertEqual(set(req['properties']), set(pf.REQUIREMENTS) | set(pf.REQUIREMENTS_OPTIONAL))
        self.assertEqual(set(schema['properties']['image_checks']['required']), set(pf.IMAGE_CHECKS))
        aa = schema['properties']['requirements']['properties']['antialiasing']['oneOf'][1]
        self.assertEqual(set(aa['required']), set(pf.ANTIALIASING))
        self.assertEqual(set(aa['properties']), set(pf.ANTIALIASING) | set(pf.ANTIALIASING_OPTIONAL))
        ini = schema['properties']['requirements']['properties']['ini_keys']['items']['pattern']
        self.assertEqual(ini, '^' + pf.INI_KEY_RE.pattern + '$')
        step = schema['properties']['steps']['items']
        self.assertEqual(set(step['required']), set(pf.STEP))
        self.assertEqual(set(step['properties']), set(pf.STEP) | set(pf.STEP_OPTIONAL))
        stats = schema['$defs']['criterion']['properties']['measures']['additionalProperties']
        self.assertEqual(stats['properties']['stat']['enum'], list(pf.STATS))


if __name__ == '__main__':
    unittest.main()
