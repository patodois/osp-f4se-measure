# SPDX-License-Identifier: MIT OR Apache-2.0
import json
import subprocess
import sys

rows = json.loads(subprocess.check_output([sys.argv[1]]))
assert len(rows) == 8
assert rows[0]["value"] == 1.5 and rows[0]["type"] == "float"
assert rows[0]["found"] and rows[0]["lookup_completed"] and rows[0]["errors"] == []
assert rows[1]["name"] == "sLocalText" and rows[1]["registered_name"] == "SLocalText"
assert rows[1]["value"] == 'Synthetic "text" — 😀'
assert rows[2]["found"] and rows[2]["value"] is None and rows[2]["errors"]
assert rows[3]["value"] is None and rows[3]["errors"]
assert rows[4]["type"] == "unknown" and rows[4]["value_encoding"] == "raw_u32"
assert rows[4]["value"] == rows[4]["raw_u32"] == 0xffffffff and rows[4]["errors"]
assert rows[5]["found"] is False and rows[5]["lookup_completed"] is True
assert rows[6]["found"] is None and rows[6]["lookup_completed"] is False
assert rows[7]["found"] and rows[7]["value"] is None and rows[7]["errors"]
print("PASS: names parser, opaque-map call contract/reference release, item faults, values, Unicode, raw-u32 disclosure")
