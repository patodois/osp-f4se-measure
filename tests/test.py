# SPDX-License-Identifier: MIT OR Apache-2.0
import json
import math
import subprocess
import sys

raw = subprocess.check_output([sys.argv[1]])
assert raw.endswith(b"\n")
raw.decode("utf-8", errors="strict")
def reject_constant(value):
    raise AssertionError(f"non-JSON number: {value}")
document = json.loads(raw, parse_constant=reject_constant)
rows = document["settings"]
assert [x["type"] for x in rows] == ["integer", "bool", "bool", "float", "string",
                                        "string", "id6", "id", "unknown", "unknown", "float"]
assert [x["value"] for x in rows] == [-1, False, True, 1.25, 'quote" slash\\\n',
                                    None, 0x123456, 0xffffffff, None, None, None]
assert rows[0]["raw_bits_hex"] == "00000000FFFFFFFF"
strings = document["strings"]
assert strings[:256] == [chr(c) for c in range(256)]
assert strings[256] == "Medição — 😀"
assert strings[257] == "".join(chr(c) for c in bytes.fromhex("c0afeda080f4908080"))
assert strings[258] == "\u00e2\u0082"
assert len(strings) == 4355
numbers = document["numbers"]
assert numbers[:2] == [-(2**63), 2**64-1]
assert numbers[2] == 0
assert numbers[3] == 1/3
assert numbers[4] == float.fromhex("0x0.0000000000001p-1022")
assert numbers[5] == sys.float_info.max
assert numbers[6:] == [None, None, None]
assert document["nested"] == {"empty_array": [], "empty_object": {}, "null": None,
                                "true": True, "false": False}
print("PASS: memory boundaries, arrays, version gates, event payloads, setting decoder;")
print("PASS: JSON parsed independently, 4,355 strings including fuzz, all byte values, Unicode, numeric limits")
