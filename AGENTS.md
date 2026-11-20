# Contributor and agent guide

Read [README.md](README.md) first for what the plugin is and how a packet is
run. Keep this file and the README in English.

## Toolchain and checks

- MinGW-w64 (`x86_64-w64-mingw32-g++`, `-objdump`, `-nm`) for the DLL, a
  native C++17 compiler for the host tests, Python 3.9+ with `numpy` and
  `Pillow`.
- The external F4SE 0.6.23 package, read-only, at `F4SE_ROOT`, which has no
  default: `make` and `make inspect` stop until it is passed, and
  `tools/verify_sdk.py` refuses any copy whose 36 pinned files differ. Never
  vendor, modify, relicense or commit any of its files, and never download it
  in CI or from any build step.

Before a change is done:

```sh
make F4SE_ROOT=/path/to/f4se_0_06_23
make test
make inspect F4SE_ROOT=/path/to/f4se_0_06_23
python3 tools/check_public_tree.py
```

`make test` needs neither the SDK nor the game. Tests use synthetic data and
mock every native library, subprocess and signal they do not own.

## Map

| Path | Purpose |
| --- | --- |
| `src/plugin.cpp` | F4SE entry points, version gate, console registration, snapshot, dumps, sequencer glue, all Win32 I/O. |
| `src/foreign_call.S` | The three bridges that call game functions inside a structured-exception scope. |
| `include/osp/layouts.hpp`, `observe.hpp` | Byte layouts and checked reads of game memory. |
| `include/osp/json.hpp`, `sha256.hpp` | JSON writer and SHA-256. |
| `include/osp/settings.hpp`, `console.hpp`, `weapon.hpp` | Snapshot: setting-name list, `control.tag` parsing, equipped weapon (schema 4). |
| `include/osp/nodes.hpp`, `scene.hpp`, `scene3.hpp` | Node and scene dumps (v6 to v8). |
| `include/osp/sequencer.hpp`, `sequencer_layout.hpp` | Packet plan, closed command list, step checks, `run.json` (v9). |
| `tools/osp_packet.py`, `packet_format.py`, `result_format.py` | Packet validation, expansion, analysis and `check`. |
| `tools/node_dump.py`, `scene_dump.py`, `scene3_dump.py`, `bone_proofs.py` | Offline readers of the dumps. |
| `tools/verify_sdk.py`, `sdk-sha256.json` | The SDK pin. |
| `tools/check_public_tree.py`, `.public-tree.cfg` | The public-tree gate. |
| `docs/` | Formats ([packet](docs/packet.md), [result](docs/result.md), [snapshot](docs/format.md), dumps), the [sequencer](docs/sequencer.md), [provenance](docs/provenance.md). |

## Rules that hold

- **Clean room.** Never open, disassemble or decompile the game executable or
  its DLLs, never use Address Library, and never read another plugin's or
  library's implementation code. Facts come from the official F4SE 0.6.23
  package; the few permissive layout facts recorded and credited in
  `docs/provenance.md` (CommonLibF4, MIT) are an exception, not permission to
  consult more.
- **Provenance.** Every game address, offset, virtual slot and calling
  convention needs a citation (file and line of the pinned package) in
  `docs/provenance.md` before code uses it. Adding an SDK file means adding its
  hash to `tools/sdk-sha256.json`.
- **Never compile SDK C++ game classes with GCC.** Use checked reads of
  explicit byte layouts and Microsoft x64 function pointers through
  `src/foreign_call.S`. Release every engine fixed string you construct.
- **Missing knowledge gives an explicit incomplete result**, never guessed
  data: a `null` with its reason.
- **No invented engine API.** The SDK publishes no console command runner, no
  argument parser, no frame counter and no hit events. Do not add a command
  runner, frame hook or handler call without a published contract; record what
  is missing instead. Console input reaches the plugin only as `ospsnap` and
  `ospnodes` (no arguments) and the atomic `control.tag` file.
- **Writes stay in the plugin's folders**: `osp::seq::child` guards every path
  under `Data\F4SE\Plugins\osp_measure\runs`, the snapshot files and the log
  stay where `docs/format.md` says. No work in `DllMain`; game reads only on
  the F4SE or console callback thread.
- **Frozen packets.** A packet's criterion is fixed before measurement. Never
  edit a packet version that has been measured or published; add a new
  `v<N>/` version and keep the old one with its results. The closed command
  list lives in both `tools/packet_format.py` and `include/osp/sequencer.hpp`;
  change both and the shared corpus test together. A part file with
  `Screenshot` holds only `Screenshot` and `ospsnap`. Setting names in a packet
  come only from a run's evidence, never from memory. INI settings other than
  the antialiasing requirement are recorded, never required.
- **Results without images.** Results carry numbers and hashes only; a `null`
  needs its reason; `retrospective` results are never admissible.
- **Serialized formats are pinned.** Existing snapshot and dump bytes stay
  identical (the legacy tests pin them); a new field goes into a new schema or
  version.
- **Nothing private in Git**: game files, saves, screenshots or any image,
  run folders, setting-name lists, snapshots, built DLLs, local paths,
  secrets. Put local artifacts under `build/`, which Git ignores. The gate
  rejects them; run it before every commit.
- **Live runs** (the game, Wine, installing the DLL) happen only on explicit
  request from the maintainer. Coding work uses the host tests.

## Commits

Commit as `patodois <patodois@users.noreply.github.com>`, on a branch, only
when `make test` and the gate pass, and end the message with the trailer of
the model that wrote the change (for example
`Co-Authored-By: Claude <noreply@anthropic.com>`). Original code is
MIT OR Apache-2.0; F4SE's terms stay separate.
