# Measurement packet, schema 1

Original code is MIT OR Apache-2.0. A packet is data only: it never carries
code, game files or images. One packet asks one question about one rule.

- Structural schema: [`schemas/packet.schema.json`](../schemas/packet.schema.json).
- Authoritative validator: `python3 tools/osp_packet.py validate <packet.json>`
  (`tools/packet_format.py`). It adds the cross-checks a JSON Schema cannot
  express: measures name real steps and crops, the closed command list, the
  expansion counts in `expected_outputs`, canary rules.
- First packet: version 4 is [`packets/nif_backface_cull_v1.signs/v4/packet.json`](../packets/nif_backface_cull_v1.signs/v4/packet.json)
  (SHA-256 `ef4c03ea…bfe7c4`). It is version 3 with the setting names round 26
  found and the antialiasing requirement pinned (below); it has not been run.
  Version 3, measured in round 26 (supported on the numbers, not admissible:
  the antialiasing name it asked for was not found and its text was unpinned),
  stays unchanged at [`v3/packet.json`](../packets/nif_backface_cull_v1.signs/v3/packet.json).
  Version 2, measured in round 25 (result void: antialiasing was TAA), stays
  unchanged at [`packets/nif_backface_cull_v1.signs/packet.json`](../packets/nif_backface_cull_v1.signs/packet.json);
  its template `photo_v1` is retired, so it can be analysed but not built.
  A measured version is never edited: every change is a new version in its own
  `v<N>/` folder.

## Fields

| Field | Meaning |
| --- | --- |
| `packet_schema`, `id`, `version` | Format 1; a lowercase id (`[a-z0-9][a-z0-9_.-]{0,63}`); a version that changes whenever any byte of the packet changes. |
| `rule`, `rule_status_before` | The rule identifier in the engine project's hypothesis list, and its status when the packet was written. |
| `question` | One sentence ending in `?`. |
| `estimated_minutes` | Time a person needs, including settling. |
| `requirements` | Game 1.10.163.0, F4SE 0.6.23 release 25, minimum plugin version 9; from `photo_v2` on the `executable` (`Fallout4.exe` size and SHA-256, checked by the plugin before any step); the exact active plugin list in load order (the bench plugin last); resolution; antialiasing (below); `data_unmodded: true`; the master and archive names whose hashes the result must carry; the INI keys whose effective values the plugin records (`ini_keys`: exact setting names, `name:Section` or, as round 26 found the autosave settings, `name` without a section; only an exact name is ever found). |
| `requirements.antialiasing` | Versions 1–2: a mode name. From `photo_v2` on: `key`, the exact setting name whose effective value decides the requirement (listed in `ini_keys`; required once the text is pinned; without it, as in version 3, `sAntiAliasing:Display`); `mode`; `value_text`, the exact effective value of that setting that satisfies the requirement; `value_text_pinned`, true only once a run has recorded that text with the mode set; `other_mode_texts`, texts observed for other modes (they violate the requirement); `evidence`, where the text comes from. While the text is unpinned, a matching or unknown value is reported unverified, never met. |
| `bench_plugin` | File name inside `Data`, size and SHA-256. Our own plugin; it references game forms only (see below). |
| `cell` | The bench cell's EditorID (used by `coc`) and its six-digit local FormID inside the bench plugin. |
| `bat_prefix`, `template` | Prefix of the typed part files (`cull01a`…) and the expansion template: `photo_v2`; `photo_v1` is retired. |
| `screenshot_base_name` | File name prefix of the game's screenshots in the game folder. |
| `position_tolerance` | Largest allowed difference, in world units, between the requested and the observed player position. |
| `steps` | Ordered poses. Each has `id`, `kind` (`measure` or `canary`), `position` (three numbers, typed with nine decimals), integer `angles`, `settle_seconds`, `simulated_time` (`require`: game time must also have advanced by the settle time; `record`: recorded only), `free_camera_seconds`, and up to eight bench-local `roots` whose root transforms are recorded at capture. |
| `mask` | `corners32` excludes fixed 32×32 corners (the Round21+ convention); `none` excludes nothing. |
| `image_checks` | Required from `photo_v2` on. Photograph and repeat must agree inside the listed `crops`: each listed statistic within `max_stat_difference`, and at most `max_pixels_over_threshold` pixels differing by more than `pixel_threshold` in any channel. A crop listed here need not be used by any measure (for example a band where an overlay would appear). |
| `crops` | Named regions, each the union of one to eight integer `[x0, y0, x1, y1)` rectangles in the required resolution. Fixed in the packet, never chosen after seeing an image. |
| `criterion` | The decision rule, fixed before any measurement (below). |
| `canaries` | Already decided rules measured again in the same session, each with its own criterion and expected verdict. A failing canary voids the result. A packet without canaries must say why in `canary_exemption`. |
| `expected_outputs` | Counts the expansion must produce: parts, marks, screenshots, steps. A mismatch means the packet is wrong. |
| `notes` | Free text for people. |

## Settings: names as found, and what decides

Every name in `ini_keys` is the exact name of a setting in the game's INI or
prefs list. The plugin finds only an exact name; for a name it does not find
it records the reason and, as evidence for a later version, the settings whose
names differ only in case or in section (`candidates`). A candidate never
counts as the key. Version 4 asks only for names that round 26 found:

| Version 3 asked for | Round 26 found | Version 4 asks for |
| --- | --- | --- |
| `sAntiAliasing:Display` | `SAntiAliasing:Display` (prefs, string, `FXAA`) | `SAntiAliasing:Display` |
| `SScreenShotBaseName:Display` | `sScreenShotBaseName:Display` (ini, string, `ScreenShot`) | `sScreenShotBaseName:Display` |
| `bSaveOnTravel`, `bSaveOnWait`, `bSaveOnRest` under `:SaveGame` and `:MAIN`; `bSaveOnPause:MAIN` | `bSaveOnTravel`, `bSaveOnWait`, `bSaveOnRest`, `bSaveOnPause`, without a section (prefs, bool, all `false` because the autosave had been switched off before the run) | the four names without a section |
| `bAllowScreenShot:Display` | nothing, not even a near name | dropped |
| eleven other names | each found exactly | unchanged |

**Antialiasing** is the only setting that decides a requirement. Version 4
reads it from `SAntiAliasing:Display` (`requirements.antialiasing.key`) and
counts it met only when that exact name is found with the exact text `FXAA`;
any other text violates it, and a missing name leaves it unverified. Round 25,
under the same exact-name lookup, had found the setting as
`sAntiAliasing:Display` (with `TAA`); why the spelling differed between the
two runs is not known. If a later run finds only that spelling, the result is
not admissible and a new version is needed: the tool never reads a near name.

**Autosave settings are recorded only, never a requirement.** The plugin
aborts the run on any save (`PreSaveGame`), so a save can void a run but can
never change a verdict; what matters is detected directly. A requirement on
the settings would add no protection and would exclude runs in which nothing
was saved, and a person running the packet by hand would have to
edit their INI files to satisfy it. Switching autosave off before a run is
advised only to avoid an aborted run. `fAutosaveEveryXMins:SaveGame` and
`bDisableAutoSave:SaveGame`, found in both runs, are recorded likewise.

**`bAllowScreenShot:Display`** is dropped rather than kept as recorded only:
no setting with that name or a near name exists in either list, so it could
only ever record "not found", and the eight bound screenshots of round 26
already show that `Screenshot` works.

### Observed in round 26 (one run under Wine 10.0 on macOS, packet version 3)

These are observations of a single run, not guarantees; details in
[sequencer.md](sequencer.md#62-observed-in-round-26-wine-100-on-macos-packet-version-3).
The run reached `complete` with 32 typed parts and eight screenshots bound,
none showing console, compass or cursor. The setting names were as in the
table above. F4SE delivered four messages, all at load (`PostLoad`,
`PostPostLoad`, `GameDataReady`, `GameLoaded`), and none at the main menu,
around `coc` or later; no save message. Each step's settle part was accepted
the first time, 77–82 s of clock and 56–59 s of game time after the pose mark,
after a 45 s wait with the console closed. The parts typed blind (photograph,
repeat, restore) were all accepted, and `GameDaysPassed` had the same bits at
the photograph and the repeat of every step (10.5–11.5 s apart). Every pair
passed the photo/repeat agreement check (largest statistic difference 0.17,
no pixel over 64).

## The criterion

A **measure** is an in-frame contrast: one statistic of the `target` crop minus
the same statistic of the `control` crop, in one screenshot of one step.
Statistics are computed on encoded RGB8 pixels over the union of the crop's
rectangles, mask applied, population form:

| `stat` | Definition |
| --- | --- |
| `mean_r`, `mean_g`, `mean_b` | Channel means. |
| `mean_gray`, `std_gray` | Mean and standard deviation of `(R+G+B)/3` per pixel. |
| `red_excess` | `mean_r − (mean_g + mean_b)/2`. |

Every measure is computed on both screenshots of its step (`photo`, `repeat`).

A **condition** compares a measure's photo value with either a constant
(`{"measure", "op", "value"}`) or another measure scaled
(`{"measure", "op", "ref", "factor", "offset"?}`: `measure op factor × ref + offset`).
Operators: `<`, `<=`, `>`, `>=`.

Evaluation, in order:

1. A measure whose step is unqualified (see [result.md](result.md)) or whose
   crops are unavailable makes the criterion **inconclusive**.
2. A measure whose photo and repeat values differ by more than
   `repeat_tolerance` makes it **inconclusive**.
3. Any failed `controls` condition makes it **inconclusive** (the instrument
   could not see what it must see).
4. All `supported_if_all` hold and no `refuted_if_any` holds: **supported**.
   Any `refuted_if_any` holds and not all `supported_if_all`: **refuted**.
   Both or neither: **inconclusive**.

Contrasts inside one frame, and ratios between contrasts of the same session,
are meant to survive different GPUs, drivers and gamma. Absolute colours are
never compared.

## Closed command list

Part files may contain only these lines (verbs are case-insensitive, arguments
exact, single spaces, printable ASCII, no `;` or quotes, at most 96 characters):

| Line | Arguments |
| --- | --- |
| `player.setpos <x\|y\|z> <decimal>` | decimal with up to 7 integer and 9 fractional digits |
| `player.setangle <x\|y\|z> <integer>` | integer from −360 to 360 |
| `tfc` or `tfc 1` | |
| `tm`, `tcl`, `Screenshot` | none |
| `coc <EditorID>` | only the packet's bench cell |
| `ospsnap` | none; exactly once per part file (the step mark) |

A part file with `Screenshot` holds exactly one `Screenshot` and the mark, and
nothing else: round 25 showed that the game takes the picture after the whole
file has run, so any `tm`, `tfc`, pose or collision command in the same file,
before or after `Screenshot`, would act on the picture. Across the expanded
parts, `tm` and `tfc` are followed in typed order: every `Screenshot` must find
the interface hidden and the free camera on, and the last part must leave both
as at the start (`layout_errors` in Python, `check_layout` in the plugin).

The plugin applies the same rules to every part file before the first step
(`include/osp/sequencer.hpp`), and a shared corpus test keeps both in step.
`coc` is not in the design's original list; it was added because a packet
starts from the main menu without loading a save.

## Expansion (`photo_v2`)

The tool expands a packet into the exact typed parts. Each part is one file in
the game folder, typed as `bat <name>`:

| Part | Lines | Plugin role |
| --- | --- | --- |
| `<p>00a` | `ospsnap` | start: all refusal checks, run folder created |
| `<p>00b` | `ospsnap`, `coc <cell>` | note (the mark comes first so that the load cannot cut it off) |
| `<p>00c` | `tcl`, `ospsnap` | cell: the player's parent cell must be the bench cell |
| `<p>NNa` | three `player.setpos`, three `player.setangle`, `ospsnap` | pose: settle timing starts ("command sent") |
| `<p>NNb` | `ospsnap` | settle: position checked; refused until settled; may be typed again |
| `<p>NNc` | `tfc 1`, `ospsnap` | free: free camera requested |
| `<p>NNd` | `tm`, `ospsnap` | capture: camera and roots read, free camera checked; interface hidden |
| `<p>NNe` | `Screenshot`, `ospsnap` | photo: camera read; typed blind |
| `<p>NNf` | `Screenshot`, `ospsnap` | repeat: camera read; typed blind |
| `<p>NNg` | `tm`, `tfc 1`, `ospsnap` | restore: interface back, free camera off; camera recorded; typed blind |
| `<p>99` | `tcl`, `ospsnap` | end |

Only `<p>NNb` has no side effect, so it is the only part the plugin can refuse
and ask to be typed again. `<p>NNe`, `<p>NNf` and `<p>NNg` are typed while the
interface is hidden. The retired `photo_v1` expansion (24 parts, the
photograph inside the capture part and the repeat sharing its file with `tm`)
is kept in `tools/packet_format.py` only to describe runs made with it.

## The bench plugin

The first packet's bench plugin is `OSPBench2.esp`, 59,831 bytes, SHA-256
`9e50cf69b8a20b4872226f62c5acbc58afdbb6a952db2777d9fb703ce1265a30`. It is our
own file: a plugin header with `Fallout4.esm` as its only master, one static
object of ours that names a mesh path of the base game, one cell
(`OSPBenchCell02`) and 800 placed references. It contains no game data. It is
published as an asset of this repository's GitHub releases, next to the DLL,
never in the Git tree (the public-tree gate refuses `.esp` files).

Only those exact bytes are accepted: `osp_packet.py build --bench` checks the
size and SHA-256 before copying the file, and the plugin refuses the run at the
start mark when the file in `Data` differs. It must be active last in the load
order, after the official masters the packet lists.

## Files a person installs

`python3 tools/osp_packet.py build <packet.json> <new-folder> [--bench OSPBench2.esp]`
writes, under `<new-folder>/game-root/`:

- one `<part>.txt` per part, for the game folder (where `bat` reads them);
- `Data/F4SE/Plugins/osp_measure/packet/packet.json` and `plan.txt`;
- with `--bench`, `Data/<bench file>` after its size and SHA-256 match.

`plan.txt` is the plugin's compiled view of the packet (plan format 2): ASCII
lines, every number either exact text or an IEEE binary32 word, a space inside
a name written as `|`. It names the SHA-256 of `packet.json`, the required
executable's size and SHA-256 (`exe` line) and the SHA-256 of every part file.
`validate` and the tests check that it is exactly `compile_plan(packet)`.
The tool never writes into a game folder.
