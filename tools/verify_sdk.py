# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pin the external read-only SDK used for every game address and ABI fact."""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
manifest = json.loads(pathlib.Path(__file__).with_name("sdk-sha256.json").read_text())
for relative, expected in manifest.items():
    path = root / relative
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f"SDK differs from the verified 0.6.23 package: {path}")
print(f"PASS: {len(manifest)} external SDK inputs match the verified 0.6.23 package")
