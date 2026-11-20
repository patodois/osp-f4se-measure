# SPDX-License-Identifier: MIT OR Apache-2.0
"""Inspect the built PE without executing it or accessing the game."""
import hashlib
import pathlib
import re
import struct
import subprocess
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
assert data[:2] == b"MZ"
offset, = struct.unpack_from("<I", data, 0x3c)
assert data[offset:offset+4] == b"PE\0\0"
machine, = struct.unpack_from("<H", data, offset+4)
characteristics, = struct.unpack_from("<H", data, offset+22)
magic, = struct.unpack_from("<H", data, offset+24)
assert machine == 0x8664 and magic == 0x20b, "must be AMD64 PE32+"
assert characteristics & 0x2000, "must be a DLL"
report = subprocess.check_output(["x86_64-w64-mingw32-objdump", "-p", str(path)], text=True)
imports = set(re.findall(r"DLL Name:\s*(\S+)", report))
allowed = {"kernel32.dll", "msvcrt.dll", "shell32.dll", "bcrypt.dll", "user32.dll"}
allowed |= {f"api-ms-win-crt-{part}-l1-1-0.dll" for part in (
    "convert", "heap", "runtime", "private", "utility", "stdio", "time", "string",
    "environment", "locale")}
assert imports and {x.lower() for x in imports} <= allowed, imports
exports_block = report.split("[Ordinal/Name Pointer] Table", 1)[1].split("The Function Table", 1)[0]
exports = {line.split()[-1] for line in exports_block.splitlines()
           if re.match(r"\s*\[\s*\d+\]", line)}
assert exports == {"F4SEPlugin_Query", "F4SEPlugin_Load"}, exports
symbols = subprocess.check_output(["x86_64-w64-mingw32-nm", str(path)], text=True)
assert "osp_call_float" in symbols and "osp_call_word" in symbols
assert "__C_specific_handler" in report, "SEH handler must resolve to a system DLL"
# Verify actual PE unwind scopes, including the caller's return PC in each scope.
optional = offset+24
image_base, = struct.unpack_from("<Q", data, optional+24)
section_count, = struct.unpack_from("<H", data, offset+6)
optional_size, = struct.unpack_from("<H", data, offset+20)
sections = []
for i in range(section_count):
    s = optional+optional_size+i*40
    virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, s+8)
    sections.append((rva, raw_size, raw_offset))
def file_offset(rva):
    for start, size, raw in sections:
        if start <= rva < start+size:
            return raw+rva-start
    raise AssertionError(f"unbacked RVA {rva:x}")
pdata, pdata_size = struct.unpack_from("<II", data, optional+112+3*8)
entries = [struct.unpack_from("<III", data, file_offset(pdata)+i)
           for i in range(0, pdata_size, 12)]
symbol_values = {name: int(address, 16)-image_base
                 for address, name in re.findall(r"^([0-9a-fA-F]+)\s+[Tt]\s+(\S+)$", symbols, re.M)}
for name in ("osp_call_float", "osp_call_word", "osp_call_void2"):
    function = symbol_values[name]
    entry, = [x for x in entries if x[0] == function]
    u = file_offset(entry[2])
    version_flags, prologue, codes, frame = struct.unpack_from("<BBBB", data, u)
    assert version_flags & 7 == 1 and version_flags >> 3 & 1
    handler_offset = u+4+((codes+1)//2)*4
    handler, count, begin, end, filter_rva, target = struct.unpack_from("<IIIIII", data, handler_offset)
    assert handler == symbol_values["__C_specific_handler"]
    assert count == 1 and filter_rva == 1 and entry[0] <= target < entry[1]
    assert data[file_offset(begin):file_offset(end)] == b"\xff\xd0\x90", "scope must include call and return PC"
    assert begin <= begin+2 < end
# No source defines DllMain; only the compiler's ordinary CRT entrypoint exists.
assert not re.search(r"\bDllMain\s*\(", pathlib.Path("src/plugin.cpp").read_text())
print("PASS: AMD64 PE32+ DLL; exports: " + ", ".join(sorted(exports)))
print("PASS: system imports only: " + ", ".join(sorted(imports)))
print("PASS: all three foreign-call bridges have SEH scopes covering call and return PC")
print("SHA256: " + hashlib.sha256(data).hexdigest())
path.with_suffix(".pe.txt").write_text(report)
