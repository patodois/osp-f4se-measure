# SPDX-License-Identifier: MIT OR Apache-2.0
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "compare_settings.py"
spec = importlib.util.spec_from_file_location("compare_settings", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def baseline(kind, value):
    return {"type": kind, "file_value": value, "formid": "00000001"}


def item(name, kind, value, found=True, queried=True, **fields):
    return dict(name=name, type=kind, value=value, found=found,
                lookup_completed=queried, errors=[], **fields)


def snapshot(*items, complete=True):
    return {"schema_version": 2, "game_settings": {"coverage": "configured_names",
            "complete": complete, "items": list(items)}}


class CompareTests(unittest.TestCase):
    def test_all_categories_and_raw_u32(self):
        reference = {"fSame": baseline("f", 1.5), "iChanged": baseline("i", 4),
                     "bAbsent": baseline("b", True), "iFault": baseline("i", 1),
                     "sLocalized": baseline("s", {"string_id": 42}),
                     "uBits": baseline("u", 0xffffffff), "fUnreadable": baseline("f", 1.0)}
        observed = snapshot(item("fSame", "float", 1.5), item("iChanged", "integer", 5),
                            item("bAbsent", None, None, found=False),
                            item("iFault", None, None, found=None, queried=False),
                            item("sLocalized", "string", "Synthetic text"),
                            item("uBits", "unknown", 0xffffffff, value_encoding="raw_u32"),
                            item("fUnreadable", "float", None),
                            item("iExtra", "integer", 1))
        report = module.compare(reference, observed)
        self.assertEqual(report["summary"], dict(equal=2, different=1, not_found=1, not_queried=1, not_compared=2))
        self.assertEqual(sum(report["summary"].values()), 7)
        self.assertEqual(report["differences"][0]["file_value"], 4)
        self.assertEqual(report["differences"][0]["current_value"], 5)
        self.assertEqual(report["extra_runtime_names"], 1)
        self.assertEqual(report["raw_u32_comparisons"], 1)

    def test_missing_observations_are_not_missing_settings(self):
        report = module.compare({"iAbsent": baseline("i", 4)}, snapshot(complete=False))
        self.assertEqual(report["summary"]["not_queried"], 1)
        self.assertEqual(report["summary"]["not_found"], 0)
        self.assertFalse(report["snapshot_settings_complete"])

    def test_no_bool_integer_aliasing_or_guessed_unsigned_type(self):
        report = module.compare({"bFlag": baseline("b", True), "uRaw": baseline("u", 1)},
                                snapshot(item("bFlag", "integer", 1), item("uRaw", "unknown", 1)))
        self.assertEqual(report["summary"]["different"], 1)
        self.assertEqual(report["summary"]["not_compared"], 1)

    def test_float_comparison_is_exact_unless_requested(self):
        reference = {"fClose": baseline("f", 1.0)}
        observed = snapshot(item("fClose", "float", 1.0000001))
        self.assertEqual(module.compare(reference, observed)["summary"]["different"], 1)
        self.assertEqual(module.compare(reference, observed, abs_tol=1e-6)["summary"]["equal"], 1)

    def test_bad_inputs(self):
        for tolerance in (-1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                module.compare({}, snapshot(), abs_tol=tolerance)
        with self.assertRaises(ValueError):
            module.compare({}, snapshot(item("iDup", "integer", 1), item("iDup", "integer", 2)))
        with self.assertRaises(ValueError):
            module.compare({"fInvalid": {}}, snapshot())
        with self.assertRaises(ValueError):
            module.compare({}, {})
        report = module.compare({"iInvalidFound": baseline("i", 0)},
                                snapshot(item("iInvalidFound", None, None, found=0)))
        self.assertEqual(report["summary"]["not_queried"], 1)

    def test_cli_and_input_overwrite_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference, observed, output = root/"file.json", root/"runtime.json", root/"report.json"
            reference.write_text(json.dumps({"iSynthetic": baseline("i", 4)}))
            observed.write_text(json.dumps(snapshot(item("iSynthetic", "integer", 8))))
            subprocess.run([sys.executable, str(SCRIPT), str(reference), str(observed), "--output", str(output)],
                           check=True, capture_output=True)
            self.assertEqual(json.loads(output.read_text())["summary"]["different"], 1)
            process = subprocess.run([sys.executable, str(SCRIPT), str(reference), str(observed),
                                      "--output", str(reference)], capture_output=True)
            self.assertEqual(process.returncode, 2)
            self.assertEqual(json.loads(reference.read_text())["iSynthetic"]["file_value"], 4)


if __name__ == "__main__":
    unittest.main()
