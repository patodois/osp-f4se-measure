# osp_measure

An F4SE plugin that measures how Fallout 4 behaves, for people who own the
game and want to help check the rules of a clean-room reader of its data.

The reader and renderer is [osp-fo4](https://github.com/patodois/osp-fo4). It
accepts a rule about the game's behaviour (for example "a single-sided sign is
not drawn from behind") only when the running game has been measured. This
plugin and its **measurement packets** let anyone with the game take that
measurement: a packet asks one question, fixes its decision rule before anyone
measures, and turns a short session of typed console commands into a result
made of numbers and hashes.

It supports exactly **Fallout 4 1.10.163.0** (the Steam build before the 2024
update) with **F4SE 0.6.23**, on Windows and on a Mac through Wine.

## What it never does

- It contains no game files and no decompiled or disassembled code. Every game
  address and memory layout it uses is published in the F4SE 0.6.23 source
  package and cited, file and line, in [docs/provenance.md](docs/provenance.md).
  It uses no Address Library and no other plugin's code.
- It never opens a network connection. The DLL imports only Windows system
  libraries, and `make inspect` checks that list.
- It never writes a save, an INI file or anything in the game's data. It writes
  only its own files: `Data\F4SE\Plugins\osp_measure\runs\` (packet runs),
  `Documents\My Games\Fallout4\F4SE\osp-measure\` (snapshots) and its log,
  `osp_measure.log`, beside F4SE's own logs. In memory it changes one thing: it
  takes over a console command slot that the SDK names as unused, to add the
  `ospsnap` and `ospnodes` commands.
- It never types, clicks or runs console commands. The person types every
  command; the plugin checks what happened and says what to type next.
- A result never contains an image. Screenshots stay on the disk of the person
  who measured; the result keeps their names, sizes, SHA-256 and crop
  statistics.

The plugin reads game memory through checked reads, and calls a game function
only where the SDK publishes its address and signature. It cannot repair a game
that is already unstable.

## Requirements

To **run** a packet:

- Fallout 4 1.10.163.0 whose `Fallout4.exe` matches the size and SHA-256 in
  the packet (65,503,104 bytes, `55f57947…9c36` for the first packet), with the
  six official add-ons and no mods.
- F4SE 0.6.23 installed as its own readme describes.
- `osp_measure.dll` (build it below, or take it from a release).
- The bench plugin the packet names, `OSPBench2.esp` for the first packet
  (see below).
- Python 3.9 or newer with `numpy` and `Pillow`, on any machine, to prepare the
  files and analyse the run.

To **build** the DLL:

- `make`, Python 3, a native C++17 compiler for the tests, and MinGW-w64
  (`x86_64-w64-mingw32-g++`, `-objdump`, `-nm`).
- The official F4SE 0.6.23 package, extracted anywhere. The build only reads
  its headers and refuses any other version: `tools/sdk-sha256.json` pins the
  SHA-256 of the 36 files it relies on. The package is not part of this
  repository and must never be committed.

### The bench plugin

`OSPBench2.esp` is our own file, made for these measurements: a plugin header
with `Fallout4.esm` as its only master, one static object of ours that names a
mesh path of the base game, one cell and 800 placed references. It
contains no game data. It is published as an asset of this repository's GitHub
releases, next to the DLL, and is not in the Git tree.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `OSPBench2.esp` | 59,831 bytes | `9e50cf69b8a20b4872226f62c5acbc58afdbb6a952db2777d9fb703ce1265a30` |

The packet pins these bytes: `osp_packet.py build --bench` refuses any other
file, and the plugin refuses the run at its first step when the file in `Data`
differs. It must be active last in the load order.

## Build

```sh
make F4SE_ROOT=/path/to/f4se_0_06_23   # build/osp_measure.dll
make test                              # host tests; need no SDK and no game
make inspect F4SE_ROOT=/path/to/f4se_0_06_23
```

`F4SE_ROOT` has no default: `make` and `make inspect` stop with a message
until you pass the folder of your extracted package. `make test` needs no SDK.
`make inspect`
checks that the DLL is AMD64 PE32+, exports exactly `F4SEPlugin_Query` and
`F4SEPlugin_Load`, imports only system DLLs, and that every call into the game
sits inside a structured-exception scope. The link step writes no timestamp,
so the same toolchain gives the same bytes: with MinGW-w64 GCC 16.2.0 this
commit builds a DLL with SHA-256
`dafeaabe31c32dbcb4b2520ba79c4d440a319f786d95b7574740cb9771e66584`.

The tests run on the host with synthetic data. They cover the JSON writer, the
setting lookups, the snapshot and dump serializers (pinned byte for byte), the
sequencer's checks, and the packet and result tools. They do not load the game
or Wine.

## Run a packet

The first packet is
[`packets/nif_backface_cull_v1.signs/v4/packet.json`](packets/nif_backface_cull_v1.signs/v4/packet.json).
It photographs two signs from the front and from behind in a bench cell, four
poses with two screenshots each, and takes about 16 minutes. The
[packet format](docs/packet.md) explains every field; the
[sequencer](docs/sequencer.md) explains what the plugin checks at each step.

### 1. Prepare the files (any machine)

```sh
python3 tools/osp_packet.py validate packets/nif_backface_cull_v1.signs/v4/packet.json
python3 tools/osp_packet.py build packets/nif_backface_cull_v1.signs/v4/packet.json \
    my-install --bench /path/to/OSPBench2.esp
```

`build` writes into a new folder and never into the game. `my-install/game-root/`
then holds 32 part files (`cull00a.txt` … `cull99.txt`), the packet with its
compiled `plan.txt`, and the bench plugin after its hash was checked.

### 2. Install (game closed)

| Copy from `my-install/game-root/` | To the game folder (where `Fallout4.exe` is) |
| --- | --- |
| every `cull*.txt` | the game folder itself, where `bat` reads them |
| `Data/F4SE/Plugins/osp_measure/packet/` | `Data\F4SE\Plugins\osp_measure\packet\` |
| `Data/OSPBench2.esp` | `Data\` |

Then:

- copy `osp_measure.dll` to `Data\F4SE\Plugins\`;
- activate `OSPBench2.esp` last in `Plugins.txt`, after the official add-ons
  and nothing else;
- in `Fallout4Prefs.ini`, under `[Display]`, set `sAntiAliasing=FXAA` and the
  resolution 1280×720 (`iSize W=1280`, `iSize H=720`): the screenshots must
  have exactly that size;
- optionally switch autosave off. A save during the run aborts it (the plugin
  sees the save message), but the settings themselves are only recorded.

### 3. Measure

Start the game through `f4se_loader.exe` and stay at the main menu: do not
load a save. Open the console and type:

```text
bat cull00a     checks everything; prints "accepted" or "REFUSED" and why
bat cull00b     loads the bench cell; wait for the load
bat cull00c     collision off; confirms the bench cell
bat cull01a     pose of step 1; close the console, wait 40 s, open it
bat cull01b     settle check; if refused, wait and type it again
bat cull01c     free camera; close the console, wait 3 s, open it
bat cull01d     reads the camera; hides the interface
bat cull01e     typed blind: the photograph
bat cull01f     typed blind: the repeat
bat cull01g     typed blind: interface and camera back
...             steps 2 to 4 the same (cull02a … cull04g)
bat cull99      collision on; run complete
```

You do not need to remember the list: every message of the plugin starts with
`osp:`, and the last one always says `osp: next: bat <part>`. Parts `e`, `f`
and `g` are typed while the interface is hidden, so the console text is not
visible; type them carefully. The plugin refuses a settle part that comes too
early, and aborts the run, with the reason in `run.json`, when something
cannot be repaired (wrong order, wrong cell, a save, a load). Quit without
saving at the end.

**On a Mac with Wine** the steps are the same: the game, F4SE and the plugin
run unchanged inside the Wine prefix, and the game folder, `Data` and the INI
files are the ones inside that prefix. Every recorded run so far was made under
Wine 10.0 on macOS; no run has been made on Windows yet, so a first Windows
result is especially useful. The console key is the one left of `1` on a US layout; with
another keyboard layout, switch the input source to US while typing.

### 4. Analyse

Copy the run folder (`Data\F4SE\Plugins\osp_measure\runs\<packet id>-<session>\`)
and the eight `ScreenShot*.png` files the game wrote in its folder to another
place, then:

```sh
python3 tools/osp_packet.py analyze <run copy> \
    --packet packets/nif_backface_cull_v1.signs/v4/packet.json \
    --screenshots <screenshot copies> --game-dir "<game folder>" --out result.json
python3 tools/osp_packet.py check result.json \
    --packet packets/nif_backface_cull_v1.signs/v4/packet.json
```

`analyze` only reads: the run, the screenshots and, with `--game-dir`, the
hashes of the masters and archives the packet names and the list of `Data`
directories (needed to show the data is unmodded). `check` recomputes the whole
decision from the numbers in `result.json` alone.

To uninstall, remove the `cull*.txt` files, the `osp_measure\packet` folder,
`OSPBench2.esp` (and its line in `Plugins.txt`) and, if you wish, the DLL and
the screenshots. Without the `packet` folder the plugin goes back to its
snapshot behaviour.

## What a result contains

`result.json` ([format](docs/result.md)) holds:

- the packet's identity, criterion and requirements, copied so that `check`
  needs nothing else;
- per step: requested and observed positions, settle times on the clock and in
  game time, the measured camera as exact float words, and per screenshot its
  file name, size, SHA-256, dimensions and the statistics of each crop;
- the verdict (`supported`, `refuted`, `inconclusive` or `void`), whether the
  result is admissible, and every reason;
- an environment fingerprint: game, F4SE and plugin versions and hashes, the
  active plugin list, the recorded INI values, the Windows or Wine version, the
  display adapter name and the window size. Fields nobody could fill are `null`
  with a reason, never a guess.

It never contains pixels, absolute paths, account names or save data. Read it
before you share it.

## How results are accepted

A result is **admissible** only when the run is complete, every requirement is
met (executable, plugin list, antialiasing, unmodded data), every step
qualifies and `check` agrees. A verdict holds only for the environment that
was measured. The engine project accepts a rule only from two independent
admissible results, or one plus the maintainer's
([result.md](docs/result.md#verdict-and-admissibility)). A packet whose version
has been measured is never edited: a change is a new version beside it.

## Other functions

Without an installed packet the plugin does what its earlier versions did:

- a JSON **snapshot** on every saved-game load or new game, and on `ospsnap`:
  versions, loaded plugins, the player's actor values and level, the equipped
  weapon instance, and the current values of the game settings named in an
  optional `osp_measure_settings.txt` beside the DLL ([format](docs/format.md));
- stationary **node and scene dumps** on `ospnodes`
  ([node dump](docs/node-dump.md), [scene dump](docs/scene-dump.md),
  [schema 3 proofs](docs/round19-protocol.md)), with offline readers in
  `tools/node_dump.py`, `tools/scene_dump.py` and `tools/scene3_dump.py`;
- `tools/compare_settings.py`, which compares a snapshot's setting values with
  values exported from the game's files.

These outputs contain game data: keep them out of Git.

## Repository

| Path | Contents |
| --- | --- |
| `src/`, `include/osp/` | The plugin: entry points, checked memory reads, JSON writer, snapshot, dumps, sequencer. |
| `packets/`, `schemas/` | Measurement packets and the JSON Schemas of packets and results. |
| `tools/` | Packet tool (`osp_packet.py`), dump readers, SDK pin, public-tree gate. |
| `tests/` | Host tests with synthetic data. |
| `docs/` | Formats, the sequencer, and the provenance of every game fact. |

[AGENTS.md](AGENTS.md) has the working rules for contributors and coding
agents. The words "round N" in the documents name the numbered measurement
sessions of development; their observations are kept where a rule rests on
them.

## Licence and F4SE

The original code and documents are licensed under [MIT](LICENSE-MIT) or
[Apache-2.0](LICENSE-APACHE), at your option ([LICENSE](LICENSE)).

F4SE is not part of this project and is not relicensed. Its readme forbids
redistributing or modifying F4SE, recommends extending it with a plugin, and
requires a plugin's source code to be publicly available. This repository is
that source. It contains no F4SE file; the build reads the SDK headers from
your own copy, and a release is to ship only `osp_measure.dll`. Details, with the
sentences quoted, are in [docs/provenance.md](docs/provenance.md#f4se-terms-read-from-the-package).

Fallout 4 is a trademark of its owner; this project is not affiliated with or
endorsed by it or by the F4SE team.

## AI assistance

This project was written with AI coding assistants under human direction:
GPT-6.1 Sol (OpenAI) wrote plugin versions 1 to 8 and a private macOS test
harness that is not part of this repository; Claude Opus 5.5 (Anthropic) wrote
version 9, the measurement packets and the packet and result tools. The host
tests and the measurements of the running game are the checks; they do not
replace your own review.
