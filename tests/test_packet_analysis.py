# SPDX-License-Identifier: MIT OR Apache-2.0
"""Result format and offline tool: analysis, qualification, verdicts and check."""
import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packet_fixtures as fx  # noqa: E402
import packet_format as pf  # noqa: E402
import result_format as rf  # noqa: E402
import osp_packet  # noqa: E402

FRONT, BACK = (0.3, 0.3), (0.0, 0.0)
ALL_GOOD = dict(sign044_front=FRONT, sign044_back=BACK, sign045_front=FRONT, sign045_back=BACK)


class Analysis(unittest.TestCase):
    def run_case(self, shots=ALL_GOOD, packet=None, **kw):
        packet = packet or fx.canary_packet()
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(t, packet, shots, **kw)
            result = osp_packet.analyze(t)
            checked = osp_packet.check(Path(t) / 'result.json', Path(t) / 'packet.json')
            self.assertTrue(checked['ok'], checked)
            stored = json.loads((Path(t) / 'result.json').read_text())
            self.assertEqual(rf.validate_result(stored), [])
            return result

    def test_supported(self):
        r = self.run_case()
        self.assertEqual(r['verdict'], 'supported')
        m = r['decision']['evaluation']['measures']
        self.assertGreater(m['front_044']['photo'], 3.0)
        self.assertAlmostEqual(m['back_044']['photo'], 0.0)
        self.assertFalse(r['decision']['admissible'])  # no --game-dir: data unverified
        self.assertIn('requirement unverified: data_unmodded', r['decision']['admissibility_reasons'])

    def test_refuted_and_inconclusive(self):
        shots = dict(ALL_GOOD, sign045_back=(0.3, 0.3))
        self.assertEqual(self.run_case(shots)['verdict'], 'refuted')
        shots = dict(ALL_GOOD, sign045_back=(0.1, 0.1))
        r = self.run_case(shots)
        self.assertEqual(r['verdict'], 'inconclusive')
        self.assertIn('neither', r['decision']['evaluation']['reasons'][0])

    def test_failed_positive_control_is_inconclusive(self):
        r = self.run_case(dict(ALL_GOOD, sign044_front=(0.0, 0.0)))
        self.assertEqual(r['verdict'], 'inconclusive')
        self.assertTrue(any('control failed' in x for x in r['decision']['evaluation']['reasons']))

    def test_unstable_repeat_is_inconclusive(self):
        r = self.run_case(dict(ALL_GOOD, sign044_front=(0.3, 0.2)))
        self.assertEqual(r['verdict'], 'inconclusive')
        self.assertTrue(any('photo and repeat differ' in x for x in r['decision']['evaluation']['reasons']))

    def test_unqualified_steps(self):
        for kw, text in [(dict(settle=(10.0, 10.0)), 'wall-clock settle'),
                         (dict(settle=(41.0, 0.5)), 'simulated settle'),
                         (dict(settle=(41.0, None)), 'simulated time unavailable'),
                         (dict(position_error=0.5), 'outside tolerance'),
                         (dict(repeat_camera_x=8192.5), 'camera words changed'),
                         (dict(capture_state=0), 'not free camera')]:
            r = self.run_case(**kw)
            self.assertEqual(r['verdict'], 'inconclusive', kw)
            self.assertTrue(any(text in x for x in r['decision']['evaluation']['reasons']), (kw, r['decision']['evaluation']['reasons']))
            # The numbers stay visible; ignoring qualification they still say supported.
            self.assertEqual(r['decision']['numbers_only_verdict'], 'supported', kw)
            self.assertIn('front_044', r['decision']['evaluation']['measures'])

    def test_missing_screenshot(self):
        shots = dict(ALL_GOOD)
        del shots['sign044_back']
        r = self.run_case(shots)
        self.assertEqual(r['verdict'], 'inconclusive')
        self.assertIn('photo screenshot not bound', r['decision']['qualification']['sign044_back'])

    def test_failed_canary_voids(self):
        p = fx.with_canary(fx.canary_packet())
        good = self.run_case(dict(ALL_GOOD, canary_front=FRONT), packet=p)
        self.assertEqual(good['verdict'], 'supported')
        self.assertTrue(good['decision']['canaries'][0]['passed'])
        bad = self.run_case(dict(ALL_GOOD, canary_front=BACK), packet=p)
        self.assertEqual(bad['verdict'], 'void')
        self.assertEqual(bad['decision']['criterion_verdict'], 'supported')
        self.assertIn('canary synthetic_known_rule gave refuted', bad['decision']['void_reasons'][0])

    def test_aborted_run_voids(self):
        r = self.run_case(status='aborted')
        self.assertEqual(r['verdict'], 'void')
        r = self.run_case(status='in_progress')
        self.assertIn('run status is in_progress', r['decision']['void_reasons'])

    def test_requirement_violation_voids(self):
        p = fx.canary_packet()
        with tempfile.TemporaryDirectory() as t:
            run = fx.write_run(t, p, ALL_GOOD)
            run['environment']['active_plugins'].pop(1)
            (Path(t) / 'run.json').write_text(json.dumps(run))
            r = osp_packet.analyze(t)
            self.assertEqual(r['verdict'], 'void')
            self.assertIn('requirement violated: active_plugins', r['decision']['void_reasons'])

    def test_packet_hash_mismatch_voids(self):
        p = fx.canary_packet()
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(t, p, ALL_GOOD)
            (Path(t) / 'packet.json').write_text(json.dumps(p, indent=3))
            r = osp_packet.analyze(t)
            self.assertIn('run was made with a different packet', r['decision']['void_reasons'])

    def test_game_dir_inventory_and_hashes(self):
        p = fx.pinned(fx.canary_packet())
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(Path(t) / 'run', p, ALL_GOOD)
            data = Path(t) / 'game' / 'Data'
            (data / 'F4SE').mkdir(parents=True)
            (data / 'Fallout4.esm').write_bytes(b'esm')
            r = osp_packet.analyze(Path(t) / 'run', game_dir=Path(t) / 'game')
            self.assertTrue(r['decision']['admissible'], r['decision']['admissibility_reasons'])
            hashed = r['environment']['hashed_files']['value']
            self.assertEqual(hashed[0]['size'], 3)
            self.assertTrue(hashed[1]['missing'])
            (data / 'textures').mkdir()
            r = osp_packet.analyze(Path(t) / 'run', game_dir=Path(t) / 'game')
            self.assertIn('requirement violated: data_unmodded', r['decision']['void_reasons'])

    def test_interface_in_the_repeat_is_rejected(self):
        # Round 25: the repeat showed the console box and the compass while the photo did not.
        r = self.run_case(repeat_image=dict(overlay=True))
        self.assertEqual(r['verdict'], 'inconclusive')
        for step, failures in r['decision']['qualification'].items():
            self.assertTrue(any('photo and repeat disagree in overlay_watch' in f for f in failures), (step, failures))
            self.assertTrue(any('disagree in side_control' in f for f in failures), (step, failures))
        # The criterion's own repeat tolerance would not have caught it.
        self.assertFalse(any('photo and repeat differ by' in x for x in r['decision']['evaluation']['reasons']))
        self.assertEqual(r['decision']['numbers_only_verdict'], 'supported')
        agreement = r['steps'][0]['image_agreement']
        self.assertGreater(agreement['overlay_watch']['pixels_over_threshold'], 0)

    def test_small_overlay_is_caught_by_the_pixel_count_only(self):
        r = self.run_case(repeat_image=dict(cursor=True))
        failures = r['decision']['qualification']['sign044_front']
        self.assertEqual([f for f in failures if 'differs by' in f], [])          # means stay within 1 level
        self.assertTrue(any('pixels differ by more than 64 levels' in f for f in failures), failures)

    def test_identical_overlay_in_both_shots_is_not_caught(self):
        # What the check cannot see: an overlay present in the photo and the repeat alike.
        import hashlib
        from PIL import Image
        p = fx.canary_packet()
        with tempfile.TemporaryDirectory() as t:
            run = fx.write_run(t, p, ALL_GOOD)
            for s in run['screenshots']:
                f = Path(t) / 'screenshots' / s['file']
                Image.fromarray(fx.image(ALL_GOOD[s['bound_to']['step']][0], overlay=True)).save(f)
                raw = f.read_bytes()
                s.update(size=len(raw), sha256=hashlib.sha256(raw).hexdigest())
            (Path(t) / 'run.json').write_text(json.dumps(run))
            r = osp_packet.analyze(t)
            self.assertEqual(r['decision']['qualification'], {s['id']: [] for s in p['steps']})

    def test_photo_camera_must_match_the_capture(self):
        r = self.run_case(photo_camera_x=8192.5)
        self.assertTrue(any('changed between capture and photo' in f
                            for f in r['decision']['qualification']['sign044_front']))

    def test_antialiasing_value_text(self):
        # Version 3: unpinned expected text: unverified, never met; a known other mode's text: violated.
        v3 = fx.v3_packet()
        r = self.run_case(packet=v3)
        self.assertEqual(r['decision']['requirements']['antialiasing'], 'unverified')
        self.assertFalse(r['decision']['admissible'])
        self.assertEqual(r['decision']['requirement_notes']['antialiasing'],
                         dict(key='sAntiAliasing:Display', observed='FXAA', required_text='FXAA',
                              required_text_pinned=False))
        self.assertEqual(self.run_case(packet=v3, aa_value='TAA')['decision']['requirements']['antialiasing'],
                         'violated')
        self.assertEqual(self.run_case(packet=v3, aa_value='fxaa')['decision']['requirements']['antialiasing'],
                         'unverified')
        # Version 4 pins the text: only exactly FXAA is met, any other text is violated.
        p = fx.canary_packet()
        self.assertEqual(self.run_case(packet=p)['decision']['requirements']['antialiasing'], 'met')
        for text in ('fxaa', 'FXAA ', 'TAA', ''):
            self.assertEqual(self.run_case(packet=p, aa_value=text)['decision']['requirements']['antialiasing'],
                             'violated', text)

    def test_missing_ini_key_never_satisfies_a_requirement(self):
        p = fx.pinned(fx.v3_packet())
        with tempfile.TemporaryDirectory() as t:
            run = fx.write_run(t, p, ALL_GOOD)
            run['environment']['ini'] = [dict(key='sAntiAliasing:Display', found=False, collection=None, type=None,
                                              value=None, raw_bits_hex=None, reason='no setting with exactly this name',
                                              candidates=[dict(name='sAntialiasing:Display', collection='prefs')]),
                                         dict(key='bAllowScreenShot:Display', found=False, reason='not found',
                                              candidates=[])]
            (Path(t) / 'run.json').write_text(json.dumps(run))
            r = osp_packet.analyze(t)
            self.assertEqual(r['decision']['requirements']['antialiasing'], 'unverified')
            notes = {n['key']: n for n in r['decision']['requirement_notes']['missing_ini_keys']}
            self.assertEqual(notes['sAntiAliasing:Display']['weakens'], 'antialiasing')
            self.assertEqual(notes['sAntiAliasing:Display']['candidates'], ['sAntialiasing:Display'])
            self.assertEqual(notes['bAllowScreenShot:Display']['weakens'], 'nothing decided: recorded only')
            # A found key with a value of the wrong type is not a match either.
            run['environment']['ini'] = [dict(key='sAntiAliasing:Display', found=True, value=1)]
            (Path(t) / 'run.json').write_text(json.dumps(run))
            self.assertEqual(osp_packet.analyze(t)['decision']['requirements']['antialiasing'], 'unverified')

    def test_antialiasing_is_read_from_its_exact_key_only(self):
        # Round 26: the setting existed only as SAntiAliasing:Display. Version 4 is keyed to that
        # name: met by a row found under it, never by a near name recorded as a candidate.
        p = fx.canary_packet()
        self.assertEqual(p['requirements']['antialiasing']['key'], 'SAntiAliasing:Display')
        r = self.run_case(packet=p)
        self.assertEqual(r['decision']['requirements']['antialiasing'], 'met')
        self.assertEqual(r['decision']['requirement_notes']['antialiasing']['key'], 'SAntiAliasing:Display')
        with tempfile.TemporaryDirectory() as t:
            run = fx.write_run(t, p, ALL_GOOD)
            run['environment']['ini'] = [
                dict(key='sAntiAliasing:Display', found=True, collection='prefs', type='string', value='FXAA'),
                dict(key='SAntiAliasing:Display', found=False, collection=None, type=None, value=None,
                     raw_bits_hex=None, reason='no setting with exactly this name',
                     candidates=[dict(name='sAntiAliasing:Display', collection='prefs', type='string',
                                      value='FXAA')])]
            (Path(t) / 'run.json').write_text(json.dumps(run))
            r = osp_packet.analyze(t)
            self.assertEqual(r['decision']['requirements']['antialiasing'], 'unverified')
            notes = {n['key']: n for n in r['decision']['requirement_notes']['missing_ini_keys']}
            self.assertEqual(notes['SAntiAliasing:Display']['weakens'], 'antialiasing')
            self.assertNotIn('sAntiAliasing:Display', notes)

    def test_antialiasing_key_rules(self):
        p = fx.v3_packet()
        p['requirements']['antialiasing']['value_text_pinned'] = True
        self.assertTrue(any('key is required' in e for e in pf.validate_packet(p)))
        p = fx.pinned(fx.v3_packet())
        p['requirements']['antialiasing']['key'] = 'SAntiAliasing:Display'
        self.assertTrue(any('listed in ini_keys' in e for e in pf.validate_packet(p)))
        p['requirements']['antialiasing']['key'] = 'a:b:c'
        self.assertTrue(any('must be a setting name' in e for e in pf.validate_packet(p)))

    def test_version_3_run_never_passes_as_version_4(self):
        # Round 26 measured version 3. Read against version 4 it is void (another packet) and, as a
        # retrospective, never admissible; the setting it recorded only as a candidate does not
        # satisfy version 4's antialiasing requirement either.
        with tempfile.TemporaryDirectory() as t:
            run = fx.write_run(Path(t) / 'run', fx.v3_packet(), ALL_GOOD, packet_bytes=fx.V3_PACKET.read_bytes())
            run['environment']['ini'] = [dict(
                key='sAntiAliasing:Display', found=False, collection=None, type=None, value=None, raw_bits_hex=None,
                reason='no setting with exactly this name in the INI or prefs setting list at start',
                candidates=[dict(name='SAntiAliasing:Display', collection='prefs', type='string', value='FXAA')])]
            (Path(t) / 'run' / 'run.json').write_text(json.dumps(run))
            for kind in ('sequencer_run', 'retrospective'):
                out = Path(t) / ('%s.json' % kind)
                r = osp_packet.analyze(Path(t) / 'run', packet_path=fx.V4_PACKET, kind=kind, out=out)
                d = r['decision']
                self.assertEqual((r['packet']['version'], r['verdict'], d['criterion_verdict']), (4, 'void', 'supported'))
                self.assertFalse(d['admissible'])
                self.assertIn('run was made with a different packet', d['void_reasons'])
                self.assertEqual(d['requirements']['antialiasing'], 'unverified')
                self.assertEqual(kind == 'retrospective',
                                 'result kind is retrospective, not sequencer_run' in d['admissibility_reasons'])
                self.assertTrue(osp_packet.check(out, fx.V4_PACKET)['ok'])
            # Relabelling the retrospective as a sequencer run does not make it admissible.
            r = json.loads((Path(t) / 'retrospective.json').read_text())
            r['kind'] = 'sequencer_run'
            r['decision']['admissibility_reasons'].remove('result kind is retrospective, not sequencer_run')
            (Path(t) / 'relabelled.json').write_text(json.dumps(r))
            checked = osp_packet.check(Path(t) / 'relabelled.json', fx.V4_PACKET)
            self.assertTrue(checked['ok'])
            self.assertFalse(checked['admissible'])
            # The stored version 3 result itself is refused against version 4.
            osp_packet.analyze(Path(t) / 'run')
            self.assertFalse(osp_packet.check(Path(t) / 'run' / 'result.json', fx.V4_PACKET)['ok'])

    def test_executable_requirement(self):
        r = self.run_case()
        self.assertEqual(r['decision']['requirements']['executable'], 'met')
        r = self.run_case(executable=dict(file='Fallout4.exe', size=65503104, sha256='0' * 64))
        self.assertIn('requirement violated: executable', r['decision']['void_reasons'])

    def test_settle_timing_origin_is_carried(self):
        r = self.run_case()
        self.assertEqual(r['steps'][0]['settle']['timing_origin']['role'], 'pose')
        self.assertIsNotNone(r['steps'][0]['photo_to_repeat_seconds'])

    def test_version_2_runs_stay_analysable(self):
        # A photo_v1 run (round 25's layout) is analysed with its own packet and checks unchanged.
        p = fx.v2_packet()
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(t, p, ALL_GOOD, packet_bytes=fx.V2_PACKET.read_bytes())
            r = osp_packet.analyze(t)
            self.assertNotIn('image_checks', r)
            self.assertNotIn('camera_photo', r['steps'][0])
            self.assertEqual(r['decision']['requirements']['antialiasing'], 'met')
            self.assertEqual(r['verdict'], 'supported')
            self.assertTrue(osp_packet.check(Path(t) / 'result.json', fx.V2_PACKET)['ok'])
            out = Path(t) / 'elsewhere.json'
            osp_packet.analyze(t, out=out)
            self.assertTrue(out.is_file())
            with self.assertRaises(rf.ResultError):
                osp_packet.analyze(t, out=out)

    def test_check_detects_tampering(self):
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(t, fx.canary_packet(), ALL_GOOD)
            osp_packet.analyze(t)
            path = Path(t) / 'result.json'
            r = json.loads(path.read_text())
            r['verdict'] = 'refuted'
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path)['ok'])
            r = json.loads((Path(t) / 'result.json').read_text())
            r['verdict'] = 'refuted'
            r['decision']['verdict'] = 'refuted'
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path)['ok'])
            osp_packet.analyze(t)
            r = json.loads(path.read_text())
            r['steps'][1]['shots']['photo']['crops']['sign_band']['red_excess'] = 40.0
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path)['ok'])
            osp_packet.analyze(t)
            r = json.loads(path.read_text())
            r['criterion']['supported_if_all'][0]['factor'] = 5.0
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path, Path(t) / 'packet.json')['ok'])
            osp_packet.analyze(t)
            r = json.loads(path.read_text())
            r['image_checks']['max_stat_difference'] = 99.0
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path, Path(t) / 'packet.json')['ok'])
            osp_packet.analyze(t)
            r = json.loads(path.read_text())
            r['steps'][0]['image_agreement']['overlay_watch']['pixels_over_threshold'] = 5
            path.write_text(json.dumps(r))
            self.assertFalse(osp_packet.check(path)['ok'])

    def test_result_validation(self):
        with tempfile.TemporaryDirectory() as t:
            fx.write_run(t, fx.canary_packet(), ALL_GOOD)
            r = osp_packet.analyze(t)
        bad = copy.deepcopy(r)
        bad['environment']['gpu'] = dict(value=None, filled_by='none', reason='')
        self.assertTrue(any('null without a reason' in e for e in rf.validate_result(bad)))
        bad = copy.deepcopy(r)
        bad['steps'][0]['shots']['photo']['png'] = 'AAAA'
        self.assertTrue(any('image data' in e for e in rf.validate_result(bad)))
        bad = copy.deepcopy(r)
        bad['verdict'] = 'probably'
        self.assertTrue(rf.validate_result(bad))

    def test_crop_statistics_union_and_mask(self):
        import numpy as np
        a = np.zeros((720, 1280, 3), np.uint8)
        a[0:2, 0:4] = (30, 10, 20)
        s = rf.crop_statistics(a, [[0, 0, 4, 2], [2, 0, 4, 2]])
        self.assertEqual(s['pixels'], 8)
        self.assertEqual(s['red_excess'], 15.0)
        s = rf.crop_statistics(a, [[0, 0, 4, 2]], rf.image_mask('corners32', 1280, 720))
        self.assertEqual(s, dict(available=False, pixels=0))
        with self.assertRaises(rf.ResultError):
            rf.crop_statistics(a, [[0, 0, 1281, 2]])

    def test_cli(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.cli()

    def cli(self):
        self.assertEqual(osp_packet.main(['validate', str(fx.CANARY_PACKET)]), 0)
        with tempfile.TemporaryDirectory() as t:
            bad = Path(t) / 'bad.json'
            p = fx.canary_packet()
            p['steps'][0]['angles'] = [0, 0, 999]
            bad.write_text(json.dumps(p))
            self.assertEqual(osp_packet.main(['validate', str(bad)]), 1)
            fx.write_run(Path(t) / 'run', fx.canary_packet(), ALL_GOOD)
            self.assertEqual(osp_packet.main(['analyze', str(Path(t) / 'run')]), 0)
            self.assertEqual(osp_packet.main(['check', str(Path(t) / 'run' / 'result.json')]), 0)
            out = Path(t) / 'retrospective.json'
            self.assertEqual(osp_packet.main(['analyze', str(Path(t) / 'run'), '--retrospective', '--out', str(out)]), 0)
            r = json.loads(out.read_text())
            self.assertEqual(r['kind'], 'retrospective')
            self.assertFalse(r['decision']['admissible'])
            self.assertEqual(osp_packet.main(['check', str(out)]), 0)

class Schema(unittest.TestCase):
    def test_structural_schema_matches_validator_fields(self):
        schema = json.loads((fx.ROOT / 'schemas' / 'result.schema.json').read_text())
        self.assertEqual(set(schema['required']), set(rf.RESULT_KEYS))
        self.assertEqual(set(schema['properties']), set(rf.RESULT_KEYS) | set(rf.RESULT_OPTIONAL))
        self.assertEqual(schema['$defs']['verdict']['enum'], list(rf.VERDICTS))


if __name__ == '__main__':
    unittest.main()
