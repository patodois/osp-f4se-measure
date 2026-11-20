#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Compare list-based runtime observations with an owner-provided GMST export.

Inputs and generated reports contain game data: keep them under ignored build/.
Only Python's standard library is required. No game is launched or modified.
"""
import argparse
import json
import math
from pathlib import Path
import sys


def read_json(path):
    def reject_constant(value):
        raise ValueError(f"non-JSON numeric constant {value}")
    return json.loads(Path(path).read_text(encoding="utf-8"), parse_constant=reject_constant)


def compare(baseline, snapshot, *, rel_tol=0.0, abs_tol=0.0):
    if not math.isfinite(rel_tol) or not math.isfinite(abs_tol) or rel_tol < 0 or abs_tol < 0:
        raise ValueError("tolerances must be finite and nonnegative")
    if not isinstance(baseline, dict) or not isinstance(snapshot, dict):
        raise ValueError("both JSON roots must be objects")
    section = snapshot.get("game_settings")
    if not isinstance(section, dict) or not isinstance(section.get("items"), list):
        raise ValueError("snapshot must contain game_settings.items")
    observations = {}
    for item in section["items"]:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            raise ValueError("every runtime item must have a string name")
        name = item["name"]
        if name in observations:
            raise ValueError(f"duplicate runtime observation for {name}")
        observations[name] = item
    summary = {key: 0 for key in ("equal", "different", "not_found", "not_queried", "not_compared")}
    differences, not_found, not_queried, not_compared = [], [], [], []
    type_map = {"f": "float", "i": "integer", "b": "bool", "s": "string"}
    raw_comparisons = 0
    for name, record in baseline.items():
        if not isinstance(name, str) or not isinstance(record, dict) or not {"type", "file_value", "formid"} <= record.keys():
            raise ValueError("baseline must map names to type, file_value and formid records")
        kind, expected = record["type"], record["file_value"]
        if kind not in {"b", "f", "i", "u", "s"}:
            raise ValueError(f"unsupported baseline type {kind!r}")
        item = observations.get(name)
        if item is None or item.get("lookup_completed") is not True or type(item.get("found")) is not bool:
            summary["not_queried"] += 1
            not_queried.append({"name": name, "errors": item.get("errors", []) if item else [],
                                "reason": "no completed lookup"})
            continue
        if item["found"] is False:
            summary["not_found"] += 1
            not_found.append({"name": name, "formid": record["formid"], "errors": item.get("errors", [])})
            continue
        actual = item.get("value")
        detail = {"name": name, "formid": record["formid"], "file_type": kind,
                  "runtime_type": item.get("type"), "file_value": expected,
                  "current_value": actual, "item_errors": item.get("errors", [])}
        if kind == "s":
            summary["not_compared"] += 1
            detail["reason"] = "English .STRINGS table not supplied/decoded; file value is a string reference"
            not_compared.append(detail)
            continue
        raw_u32 = kind == "u" and item.get("type") == "unknown" and item.get("value_encoding") == "raw_u32"
        if raw_u32:
            detail["comparison_basis"] = "raw Data.u32 versus file integer; no engine unsigned-type claim"
            comparable = (type(actual) is int and 0 <= actual <= 0xffffffff and
                          type(expected) is int and 0 <= expected <= 0xffffffff)
            if comparable:
                raw_comparisons += 1
        else:
            # A known, different runtime type is a difference even if Python's
            # bool/int equality would otherwise consider the values identical.
            if item.get("type") in {"integer", "float", "bool", "string", "id", "id6"} and item.get("type") != type_map.get(kind):
                summary["different"] += 1
                detail["reason"] = "type mismatch"
                differences.append(detail)
                continue
            if kind == "b":
                comparable = item.get("type") == "bool" and type(actual) is bool and type(expected) is bool
            elif kind == "i":
                comparable = item.get("type") == "integer" and type(actual) is int and type(expected) is int
            elif kind == "f":
                comparable = (item.get("type") == "float" and type(actual) in (float, int) and
                              type(expected) in (float, int) and math.isfinite(actual) and math.isfinite(expected))
            else:
                comparable = False
        if not comparable:
            summary["not_compared"] += 1
            detail["reason"] = "unreadable, unsupported or invalid runtime/file value"
            not_compared.append(detail)
            continue
        equal = math.isclose(actual, expected, rel_tol=rel_tol, abs_tol=abs_tol) if kind == "f" else actual == expected
        summary["equal" if equal else "different"] += 1
        if not equal:
            detail["reason"] = "value mismatch"
            differences.append(detail)
    return {
        "report_schema_version": 1, "coverage": "baseline_names_only",
        "snapshot_schema_version": snapshot.get("schema_version"),
        "snapshot_settings_coverage": section.get("coverage", "legacy_or_unspecified"),
        "snapshot_settings_complete": section.get("complete", False),
        "baseline_count": len(baseline), "runtime_item_count": len(observations),
        "extra_runtime_names": len(observations.keys() - baseline.keys()),
        "float_tolerance": {"relative": rel_tol, "absolute": abs_tol},
        "strings_comparison": "not_compared_without_English_STRINGS_table",
        "raw_u32_comparisons": raw_comparisons,
        "summary": summary, "differences": differences, "not_found": not_found,
        "not_queried": not_queried, "not_compared": not_compared,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="owner GMST JSON mapping names to file_value/formid/type")
    parser.add_argument("snapshot", help="osp_measure runtime JSON")
    parser.add_argument("--output", type=Path, help="report JSON (keep game data under build/); default stdout")
    parser.add_argument("--rel-tol", type=float, default=0.0)
    parser.add_argument("--abs-tol", type=float, default=0.0)
    args = parser.parse_args(argv)
    try:
        report = compare(read_json(args.baseline), read_json(args.snapshot), rel_tol=args.rel_tol, abs_tol=args.abs_tol)
        text = json.dumps(report, ensure_ascii=True, allow_nan=False, indent=2) + "\n"
        if args.output:
            if args.output.resolve() in {Path(args.baseline).resolve(), Path(args.snapshot).resolve()}:
                raise ValueError("output must not overwrite an input")
            args.output.write_text(text, encoding="utf-8")
            print(json.dumps(report["summary"], sort_keys=True))
        else:
            sys.stdout.write(text)
    except (OSError, ValueError, TypeError, OverflowError) as error:
        parser.exit(2, f"compare_settings: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
