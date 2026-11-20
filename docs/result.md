# Measurement result, schema 1

Original code is MIT OR Apache-2.0. A result is numbers only. Screenshots stay
on the disk of the person who measured; a result carries their names, sizes,
SHA-256 and crop statistics, never pixels.

A result has two layers:

1. `run.json`, written by the plugin in its own folder after every step mark
   (run schema 2, below; schema 1 was written by the round 25 build);
2. `result.json`, written by `python3 tools/osp_packet.py analyze <folder>` from
   `run.json`, the packet and the screenshots. Structural schema:
   [`schemas/result.schema.json`](../schemas/result.schema.json);
   authoritative checks: `tools/result_format.py`.

`python3 tools/osp_packet.py check result.json [--packet packet.json]`
recomputes qualification, measures, canaries and verdict from the numbers in
the result alone, as a CI would, and fails on any difference. With `--packet`
it also requires the result's copies of the criterion, requirements, steps and
crops to equal the packet byte for byte in meaning.

## run.json (plugin)

Folder: `Data\F4SE\Plugins\osp_measure\runs\<packet id>-<session prefix>-<n>\`.

| Field | Content |
| --- | --- |
| `run_schema`, `plugin_version` | 2 and 9. Schema 2: photo_v2 roles, settle timing from the pose mark, `observed`, `screenshot_scan` and `f4se_messages`. |
| `status`, `status_reasons` | `refused` (a start check failed; nothing ran), `in_progress`, `complete`, or `aborted` with the reason. A file left `in_progress` (the game closed or crashed) is treated as aborted by the analysis. |
| `packet` | id, version, SHA-256 of `packet.json`, SHA-256 of `plan.txt`. |
| `session_id`, `run_id`, `started_utc`, `updated_utc` | Identity and times. |
| `environment` | Plugin-filled fingerprint (table below), with `errors` naming the reason for every null. |
| `preflight.checks` | Every start check: name, passed, detail. |
| `marks` | One row per `ospsnap` mark: part, role, step, outcome (`advanced`, `not_settled`, `aborted`, `ignored`), UTC, process clock seconds, settle clocks (`wall_seconds_since_pose`, `simulated_seconds_since_pose`: since the timing origin, normally the pose mark), `GameDaysPassed`/`TimeScale` words, player position words and parent cell, measured camera (all float words), requested roots, checks, `observed` (facts recorded without deciding anything: `pose_in_effect_at_pose_mark`, `pose_in_effect`, `timing_origin` `{mark, role}`, `timing_restarted`), `screenshot_scan.deferred` (screenshot files left for a later mark, with the reason) and the console lines printed. |
| `f4se_messages` | Every F4SE message received while a packet was installed, from load on: type, SDK name, data length, UTC, clock, run status, the mark it followed; at most 256, then a count of those not recorded. |
| `screenshots` | Every new screenshot file seen: name, size, SHA-256, the `{step, shot}` it was bound to, status. |
| `pending_screenshots` | Requested screenshots not yet seen. |
| `next_part` | What the plugin expects to be typed next. |

The plugin never writes an absolute path into `run.json`.

## result.json (offline tool)

| Field | Content |
| --- | --- |
| `result_schema`, `kind` | 1; `sequencer_run`, or `retrospective` (never admissible) for evidence collected without the sequencer or read against a packet version it was not made with (`analyze --retrospective`). |
| `packet`, `rule`, `question` | Packet identity and SHA-256. |
| `requirements`, `bench_plugin`, `position_tolerance`, `steps_requested`, `crops`, `mask`, `criterion`, `canaries` | Copies from the packet, so `check` needs nothing else. |
| `run` | Status, reasons, ids, the packet SHA-256 the plugin saw, SHA-256 of `run.json`, unexpected and pending screenshots. |
| `environment` | Every fingerprint field as `{value, filled_by, reason}`. A field nobody could fill is `null` with a reason, never a guess. |
| `steps` | Per step: requested pose and its float32 words; settle outcome, attempts, wall-clock and simulated seconds; observed player position words; seconds the free camera was held; the measured camera at capture and repeat (state, completeness, all words); recorded roots; per shot the file, size, SHA-256, width, height and the statistics of every crop. |
| `decision` | Verdict, criterion verdict, numbers-only verdict (the criterion's conditions applied to the measures while ignoring step qualification; never a decision), void reasons, admissibility and reasons, the status of each requirement, every step's qualification failures, every measure and condition with its numbers, and each canary. |
| `verdict` | `supported`, `refuted`, `inconclusive` or `void`. |
| `analysis` | Tool name and version; `images_included: false`. |

### Who fills each environment field

| Field | Filled by | Source | When `null` |
| --- | --- | --- | --- |
| `game_version`, `f4se_version`, `f4se_release_index` | plugin | F4SE interface at load | never, once the plugin loads |
| `plugin_version` | plugin | constant 9 | never |
| `plugin_dll` | plugin | SHA-256 of its own DLL file | file unreadable |
| `executable` | plugin | SHA-256 of the running executable's file, read as bytes only | file unreadable |
| `f4se_runtime_dll` | plugin | SHA-256 of `f4se_1_10_163.dll` in the game folder | not found |
| `bench_plugin` | plugin | size and SHA-256 of the file in `Data` | not found |
| `active_plugins` | plugin | loaded full and light plugin arrays (`GameData.h`) | DataHandler unreadable |
| `ini` | plugin | effective values in the engine's INI setting lists (`GameSettings.h/.cpp`); each key found by its exact name or not; a key not found carries `reason` and `candidates` (settings whose names differ only in case or section, with their values), which never count as the key | list unreadable |
| `start_timings` | plugin | seconds spent hashing the DLL, hashing the executable, and in the whole start | not recorded (run schema 1) |
| `system` | plugin | Windows version from `RtlGetVersion`; Wine version from `wine_get_version` when present | call unavailable |
| `gpu` | plugin | primary display adapter name from `EnumDisplayDevicesW` | call failed |
| `client_size` | plugin | the game window's client rectangle at start | no own foreground window |
| `screenshot_sizes` | tool | decoded screenshot dimensions | no readable screenshot |
| `hashed_files` | tool | SHA-256 of each master/archive the packet names, from `--game-dir` | no `--game-dir` |
| `data_inventory` | tool | top-level `Data` directories and plugin files, from `--game-dir` | no `--game-dir` |
| `gpu_driver`, `gpu_vendor` | person | `--system` JSON the person writes | not declared |

## Qualification of a step

A step's numbers enter the criterion only if all of these hold:

- its settle mark was accepted, with wall-clock seconds ≥ `settle_seconds` and,
  when `simulated_time` is `require`, simulated seconds ≥ `settle_seconds`;
- the observed player position is within `position_tolerance` of the float32
  words of the requested position, on every axis;
- the free camera was held at least `free_camera_seconds` before capture;
- the capture camera is complete and in the free state (state 3), the repeat
  camera is complete, and all their numeric words are identical; with photo_v2
  the photograph's own mark has a complete camera with the same words;
- both screenshots are bound, present, hash-identical to what the plugin saw,
  and exactly the required resolution;
- with `image_checks` (photo_v2), photograph and repeat agree inside every
  listed crop: each listed statistic within `max_stat_difference`, and no more
  than `max_pixels_over_threshold` pixels differing by more than
  `pixel_threshold` in a channel. The pixel counts are computed by `analyze`
  (`image_agreement` in each step) so that `check` needs no image. This is
  what catches an interface in one of the two shots, as in round 25's
  repeats. It cannot catch an overlay present identically in both shots, one
  outside every listed crop, or one too small for the pixel threshold to
  count and the means to show.

## Verdict and admissibility

- **void**: the run is not `complete`, the run used a different packet, a
  requirement is violated, or a canary gave other than its expected verdict.
- Otherwise the criterion's verdict (see [packet.md](packet.md)).
- **admissible** is false when the result is void, when any requirement is
  unverified (for example no `--game-dir`, the antialiasing key not found, or
  its value text not yet pinned in the packet), or when the kind is
  `retrospective`.
- Requirement states from INI values: only the antialiasing setting decides a
  requirement (`antialiasing`), read under the exact name in the packet's
  `requirements.antialiasing.key` (`sAntiAliasing:Display` when the packet has
  no `key`, as in version 3; `SAntiAliasing:Display` in version 4). A key not
  found leaves its requirement unverified, never met, even when the plugin
  recorded a setting with a near name as a candidate; `decision.requirement_notes` lists every key not
  found, with its reason, its candidates and what it weakens (for every other
  key: nothing decided, recorded only). With an unpinned `value_text`, the
  expected text or an unknown text is unverified and a text listed in
  `other_mode_texts` is violated; once pinned, any other text is violated.
- `executable` (photo_v2): met when the recorded size and SHA-256 equal the
  packet's; the plugin already refuses the run before any step otherwise.

`analyze --out <file>` writes the result elsewhere than the run folder and
refuses to overwrite, for stored evidence that must stay untouched. Accepting a rule needs
  admissible results; the acceptance rule itself (two independent results, or
  one plus the maintainer's) lives with the engine project, not here.
