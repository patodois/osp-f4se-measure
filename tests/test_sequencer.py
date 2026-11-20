# SPDX-License-Identifier: MIT OR Apache-2.0
"""Cross-checks between the plugin's sequencer logic (C++) and the offline tool.

Runs the native build/sequencer_unit only: no game, Wine or Windows API.
"""
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packet_fixtures as fx  # noqa: E402
import packet_format as pf  # noqa: E402
import osp_packet  # noqa: E402

BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else fx.ROOT / 'build' / 'sequencer_unit').resolve()
CELL = 'OSPBenchCell02'


def native(*args, data=b''):
    return subprocess.run([str(BINARY), *map(str, args)], input=data, capture_output=True, check=True).stdout


def corpus():
    base = ['player.setpos x 8192', 'player.setpos z -113.310157776', 'Player.SetPos y 1.5', 'player.setangle z 180',
            'player.setangle x -360', 'tfc', 'tfc 1', 'tm', 'TCL', 'Screenshot', 'SCREENSHOT', 'coc OSPBenchCell02',
            'ospsnap', 'OSPSNAP', 'save quick', 'qqq', 'player.setav health 1', 'player.setpos x 1e3',
            'player.setpos w 1', 'player.setangle z 1.5', 'player.setangle z 361', 'player.setangle z -0',
            'player.setangle z 0360', 'tfc 2', 'tm 1', 'coc Sanctuary', 'coc ospbenchcell02', 'tcl;save x',
            'bat other', 'ospsnap tag', ' tm', 'tm ', 'player.setpos  x 1', 'prid 14', 'ospnodes',
            'screenshot "a"', 'player.setpos x 12345678', 'player.setpos x 1.1234567891', 'player.setpos x .5',
            'player.setpos x 5.', 'player.setpos x -', 'player.setpos x --1', 'player.setpos x 1.2.3',
            'player.setpos X 1', 'player.setangle', 'tfc 1 1', 'disable', 'player.disable', 'tai', 'tgm',
            'player.setpos x 0.000000001', 'player.setpos x 1234567.123456789', 'player.setpos x -1234567.1']
    rng = random.Random(9)
    words = ['player.setpos', 'player.setangle', 'tfc', 'tm', 'tcl', 'Screenshot', 'coc', 'ospsnap', 'x', 'y', 'z',
             '1', '-1', '0', '360', '-361', '1.5', 'OSPBenchCell02', 'save', 'w', '']
    for _ in range(400):
        base.append(' '.join(rng.choice(words) for _ in range(rng.randint(1, 4))))
    return base


class Parity(unittest.TestCase):
    def test_closed_list_matches_python(self):
        lines = corpus()
        out = native('commands', data=('\n'.join(lines) + '\n').encode()).decode().split('\n')[:-1]
        self.assertEqual(len(out), len(lines))
        for line, verdict in zip(lines, out):
            try:
                pf.validate_command(line, CELL)
                expected = 'ok'
            except pf.PacketError:
                expected = 'no'
            self.assertEqual(verdict, expected, repr(line))

    def test_setting_names_match_python(self):
        # Round 26 found the autosave settings without a section; both sides accept
        # name or name:Section and nothing else.
        names = ['iSize W:Display', 'SAntiAliasing:Display', 'sScreenShotBaseName:Display', 'bSaveOnTravel',
                 'bSaveOnPause', 'fAutosaveEveryXMins:SaveGame', 'a', 'a:b', 'A' * 64, 'A' * 65, 'a:' + 'b' * 32,
                 'a:' + 'b' * 33, 'a:b:c', ':Display', 'bSaveOnTravel:', '1Key:Display', 'Key-x:Display',
                 'Key:Dis.play', 'Key:Dis play', '_Key', ' Key', 'Key ', 'K\xe9y', 'Key;x', '']
        out = native('ini', data=('\n'.join(x.replace(' ', '|') for x in names) + '\n').encode('latin-1'))
        out = out.decode().split('\n')[:-1]
        self.assertEqual(len(out), len(names))
        for name, verdict in zip(names, out):
            self.assertEqual(verdict, 'ok' if pf.INI_KEY_RE.fullmatch(name) else 'no', repr(name))
        self.assertEqual(out[:6], ['ok'] * 6)

    def test_sha256_matches_hashlib(self):
        rng = random.Random(4)
        for n in [0, 1, 55, 56, 63, 64, 65, 1000, 70000]:
            data = bytes(rng.randrange(256) for _ in range(n))
            self.assertEqual(native('sha256', data=data).decode().strip(), hashlib.sha256(data).hexdigest())

    def test_plan_parity_with_compiler(self):
        # Version 4 (setting names without a section) and the measured version 3.
        for packet in (fx.CANARY_PACKET, fx.V3_PACKET):
            p, digest = pf.load(packet)
            with tempfile.TemporaryDirectory() as t:
                path = Path(t) / 'plan.txt'
                path.write_text(pf.compile_plan(p, digest))
                got = json.loads(native('plan', path))
            self.assertTrue(got['ok'], got)
            self.assertEqual((got['id'], got['packet_sha256'], got['bench'], got['plugins'], got['ini']),
                             (p['id'], digest, 'OSPBench2.esp', 8, len(p['requirements']['ini_keys'])))
            want = [[x['name'], hashlib.sha256(pf.bat_bytes(x)).hexdigest(), x['role'], x['step'], x['shots']]
                    for x in pf.expand(p)]
            self.assertEqual(got['parts'], want)

    def test_tampered_plans_are_rejected(self):
        p, digest = pf.load(fx.CANARY_PACKET)
        plan = pf.compile_plan(p, digest)
        lines = plan.splitlines(keepends=True)
        cases = [plan.replace('osp-plan 2', 'osp-plan 1'), plan.replace('end 32', 'end 31'),
                 plan.replace(' photo sign044_front 1', ' repeat sign044_front 1'),
                 ''.join(x for x in lines if not x.startswith('exe ')),
                 plan.replace(' settle sign044_front 0 40 ', ' settle sign044_front 0 0 '),
                 plan.replace('plugin full OSPBench2.esp\n', ''),
                 plan.replace('part cull01a', 'part ..\\x'),
                 ''.join(x for x in lines if not x.startswith('part cull01f ')),
                 plan + 'part extra ' + '0' * 64 + ' note - 0\n',
                 plan[:-1]]
        with tempfile.TemporaryDirectory() as t:
            for i, text in enumerate(cases):
                path = Path(t) / ('plan%d.txt' % i)
                path.write_text(text)
                self.assertFalse(json.loads(native('plan', path))['ok'], i)

    def test_native_run_record_is_analyzed_by_the_tool(self):
        p, digest = pf.load(fx.CANARY_PACKET)
        from PIL import Image
        shots = dict(sign044_front=0.3, sign044_back=0.0, sign045_front=0.3, sign045_back=0.0)
        with tempfile.TemporaryDirectory() as t:
            folder = Path(t) / 'run'
            (folder / 'screenshots').mkdir(parents=True)
            (folder / 'packet.json').write_bytes(fx.CANARY_PACKET.read_bytes())
            rows = []
            for i, (step, red) in enumerate([(s['id'], shots[s['id']]) for s in p['steps'] for _ in (0, 1)]):
                name = 'ScreenShot%d.png' % i
                Image.fromarray(fx.image(red)).save(folder / 'screenshots' / name)
                raw = (folder / 'screenshots' / name).read_bytes()
                rows.append('%s %d %s' % (name, len(raw), hashlib.sha256(raw).hexdigest()))
            (Path(t) / 'plan.txt').write_text(pf.compile_plan(p, digest))
            (Path(t) / 'files.txt').write_text('\n'.join(rows) + '\n')
            (folder / 'run.json').write_bytes(native('simulate', Path(t) / 'plan.txt', Path(t) / 'files.txt'))
            run = json.loads((folder / 'run.json').read_text())
            self.assertEqual(run['status'], 'complete')
            self.assertEqual(len(run['marks']), 32)
            self.assertEqual(run['run_schema'], 2)
            self.assertEqual([s['bound_to']['shot'] for s in run['screenshots']], ['photo', 'repeat'] * 4)
            result = osp_packet.analyze(folder)
            self.assertEqual(result['verdict'], 'supported', result['decision']['evaluation']['reasons'])
            self.assertEqual(result['steps'][0]['settle']['timing_origin']['role'], 'pose')
            self.assertEqual(result['decision']['qualification'],
                             {s['id']: [] for s in p['steps']})
            self.assertTrue(osp_packet.check(folder / 'result.json', folder / 'packet.json')['ok'])


if __name__ == '__main__':
    if not BINARY.is_file() or not os.access(BINARY, os.X_OK):
        raise SystemExit('build/sequencer_unit missing; run make test')
    unittest.main(argv=sys.argv[:1])
