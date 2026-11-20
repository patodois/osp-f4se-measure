# Measurement sequencer (plugin v9)

Original code is MIT OR Apache-2.0. F4SE SDK terms remain separate; every SDK
fact used here is cited in [provenance.md](provenance.md).

## 1. Feasibility: can the plugin drive the console and count frames?

This section was written before any v9 plugin code. It uses only the official
F4SE 0.6.23 source package already pinned by `tools/sdk-sha256.json`, plus the
two `GameAPI` files that this round adds to the same pin (section 1.4). No game
executable, other plugin, decompilation or network source was consulted.

### 1.1 Executing a console command from the plugin

The package publishes these console-related facts, and nothing more:

| Fact | Source | What it gives |
| --- | --- | --- |
| `ObScriptCommand` record, `ObScript_Execute` callback type | `f4se/ObScript.h:8–38` | The table layout and the handler signature `(paramInfo, scriptData, thisObj, containingObj, scriptObj, locals, result, opcodeOffsetPtr)`. |
| Console and script command tables, counts, opcode bases | `f4se/ObScript.h:40–50`, `ObScript.cpp:4,6` | Where each command's handler pointer lives. |
| Registration by replacing an unused record | `f4se/Hooks_ObScript.cpp:8–53` | A new zero-argument command, as `ospsnap` already uses. |
| `ConsoleManager::VPrint` / `Print`, `g_console` | `f4se/GameAPI.h:18–29`, `GameAPI.cpp:17–34` | Text **output** to the console history. No input, compile or run member. |
| `TaskInterface::AddTask` / `AddUITask` | `f4se/PluginAPI.h:195–205`, `Hooks_Threads.cpp` | Deferred callbacks; no command text is involved. |

Nothing in the package compiles or runs a console text line. A command's
handler can be reached through the table, but every handler takes an opaque
`scriptData`/`opcodeOffsetPtr` pair holding the command's compiled parameters.
The SDK publishes neither that encoding nor a function that produces it
(`ObScript.h` declares `parse` only as `void*`). Calling `player.setpos x 1`'s
handler would therefore mean inventing a parameter block, a reference argument
and a `thisObj` contract. That is not sound.

Partial alternatives exist for one command only. `GameReferences.h:28–29`
publishes `MoveRefrToPosition`, which could move the player, but that is a
different engine path from `player.setpos` and it would not cover `setangle`,
`tfc`, `tm`, `tcl` or `Screenshot`. The package publishes no free-camera,
menu-visibility, collision or screenshot function (`GameMenus.h:80` names a
menu flag about screenshots, not a call). A sequencer that mixes one engine call
with typed commands would measure a different procedure from the one it claims.

**Route A is not available.** To make it available we would need, from a source
the project accepts:

1. a published address and calling contract for compiling and running one
   console text line (or for building an ObScript parameter block for a given
   command and calling its handler), including the object it runs in; or
2. published calls with contracts for each effect the packets need: player
   position and angle as `setpos`/`setangle` apply them, free camera on/off,
   menu visibility, collision toggle, and a screenshot request.

### 1.2 Counting frames

The package publishes no per-frame callback and no global frame counter:

- `PluginAPI.h` messaging delivers load, save, new-game and data events only
  (`kMessage_*`, lines 97–115). `GameEvents.h:226–244` declares combat, death,
  furniture, load-game, object-loaded and init-script dispatchers; none is per
  frame.
- `Hooks_Threads.cpp` drains `AddTask` callbacks inside a hook of an engine
  message-queue function, in a `while (!empty)` loop that holds the queue lock.
  How often and on which thread that function runs is not published. A task
  that queues itself again runs again in the same drain (an endless loop), so
  re-queuing is not a frame delay. A helper thread could queue one task per
  drain, but that counts drains of an unpublished pump, not frames, and the
  callback's thread is not proven to be the thread that owns the scene graph.
  The project rule keeps game reads on the console callback thread.
- `NiNodes.h:54` (`iFrameCounter`) and similar fields belong to particular
  object classes; their meaning is not published.

**The plugin counts no frames.** It measures settling in two published ways,
both evaluated when the next step command arrives on the console thread:

- wall-clock seconds from `QueryPerformanceCounter` (a Windows API, no game
  knowledge); and
- simulated seconds: the change of the `GameDaysPassed` global divided by the
  `TimeScale` global, both read from `TESGlobal::value`
  (`GameForms.h:1754–1763`) in the DataHandler `arrGLOB` array
  (`GameData.h:134`, form type 11). While the console is open the game is
  paused and game time does not advance, so this tells a person who waited with
  the console open apart from one who let the world run. Those two EditorIDs are
  names of game data, not SDK facts; if either is missing the plugin records
  `null` with a reason and the analysis applies the packet's rule for that case.

A frame count would need a published per-frame hook point or frame index, or a
live proof that the task pump runs once per rendered frame on the main thread.

### 1.3 Decision: route B, stepped

The game's own `bat <file>` command runs each prepared part of a step. The person
types one short command per part (`bat cull03b`). Every part file ends its own
work with exactly one `ospsnap` line, the plugin's existing zero-argument
command, which is the only way the plugin learns that a part ran. While a packet
is installed, `ospsnap` acts as the sequencer's step mark; without an installed
packet it keeps its v5–v8 behavior byte for byte.

The plugin cannot type, wait or issue commands. What it can do on each mark:

- before the first step, refuse a packet whose hash, bench plugin, part files or
  active plugin list do not match, and refuse any part file that contains a
  command outside the closed list;
- check that marks arrive in the planned order and that the observed state fits
  the planned part (player position against the requested pose, camera state,
  parent cell);
- refuse to advance a "settle" mark until both the wall-clock and simulated
  seconds have passed, and say how long is left;
- read the measured camera and the requested roots;
- find the screenshot files the game wrote since the previous mark, hash them
  and bind them, in order, to the `Screenshot` commands of earlier parts;
- print in the console what happened and exactly what to type next;
- write the run record in its own folder after every mark, and mark the run
  aborted, with the reason, when it cannot continue.

Packet and result formats are independent of the route: a later route A would
replace typed parts with plugin-issued commands and keep both formats.

### 1.4 SDK files added to the pin

`f4se/GameAPI.h` and `f4se/GameAPI.cpp` are added to `tools/sdk-sha256.json`
for the console print call (`ConsoleManager::Print`, RVA `0x01262F50`, and the
`g_console` pointer, RVA `0x058E0AE0`). F4SE's own `GetF4SEVersion_Execute`
(`Hooks_ObScript.cpp:30–39`) prints from inside a console handler, the same
context the plugin uses. All other facts come from files already pinned.

## 2. Installing a packet

Build the files on any machine (`python3 tools/osp_packet.py build <packet.json>
<new-folder> --bench <OSPBench2.esp>`), then copy by hand, with the game closed:

| From `<new-folder>/game-root/` | To the game folder (where `Fallout4.exe` is) |
| --- | --- |
| `<part>.txt` (32 files for versions 3 and 4 of the first packet; the same bytes in both) | the game folder itself, where `bat` reads them |
| `Data/F4SE/Plugins/osp_measure/packet/` | `Data\F4SE\Plugins\osp_measure\packet\` |
| `Data/OSPBench2.esp` | `Data\` |

Activate the bench plugin last in `Plugins.txt` and install `osp_measure.dll`
v9 in `Data\F4SE\Plugins\`. Packet version 4 needs a build that accepts
setting names without a section (the plan's `ini bSaveOnTravel`); the build
that round 26 ran (`ecc774b6…`) refuses its plan at start with "bad ini
line". Nothing in the repository writes these files; the
person does. To return
`ospsnap` to its v5–v8 behavior, remove the `osp_measure\packet` folder.

## 3. The command and the parts

There is one command, `ospsnap`, typed only inside part files. The person types
`bat <part>` for each part, in the order the plugin prints. Packet versions 3
and 4 of the first packet use the `photo_v2` parts (32 typed commands):

```text
bat cull00a     start: checks, run folder, "accepted" or "REFUSED"
bat cull00b     loads the bench cell (coc); wait for the load
bat cull00c     toggles collision off (tcl); confirms the bench cell
bat cull01a     pose of step 1 (setpos x/y/z, setangle x/y/z); settle timing starts
                close the console, wait 40 s, open it
bat cull01b     settle check; refused while not settled, then type it again
bat cull01c     free camera on (tfc 1); close the console, wait 3 s, open it
bat cull01d     camera and roots read; interface hidden (tm)
bat cull01e     typed blind: Screenshot (the photograph)
bat cull01f     typed blind: Screenshot (the repeat)
bat cull01g     typed blind: interface back (tm), free camera off (tfc 1)
...             steps 2 to 4 the same (cull02a ... cull04g)
bat cull99      collision back on (tcl); run complete
```

Every message the plugin prints starts with `osp:` and is its own console line;
the last line of every mark is `osp: next: bat <part>`. `ospsnap` is the last
line of every part except `cull00b`, where `coc` must come after the mark, so
nothing the part types can follow the hint on screen.

Why the photograph and the repeat have parts of their own: in round 25 the
game took each picture only after the whole part file had run. The version 2
repeat part (`Screenshot`, `tm`, `ospsnap`, `tfc 1`) therefore photographed the
interface that its own `tm` had already switched back on. A part with
`Screenshot` may now hold nothing but `Screenshot` and `ospsnap`, so no toggle
can share a file with a picture in either order, and the layout check below
refuses any Screenshot that does not find the interface hidden and the free
camera on.

No wait is programmed between the photograph and the repeat. Both are taken
with the console open (the world paused) and the free camera frozen, and they
are already separated by the typing of a part (round 25: 9 to 24 s between the
marks). In round 25 the rows of the frame that no overlay touched were bit
identical between photograph and repeat, under TAA, and round 24's pairs agreed
within 0.08 levels; a longer wait would only lengthen the blind typing. The
plugin records the clock and game time at both marks, and the analysis stores
the interval (`photo_to_repeat_seconds`), so a later run can show whether game
time moves while the interface is hidden.

## 4. What the plugin checks

At `start`, before any step, it **refuses** the packet (run status `refused`,
nothing else happens until the game restarts) when any of these fail:

- `plan.txt` parses as plan format 2 and has the photo_v2 structure (plan
  format 1, the retired photo_v1 layout, is refused);
- SHA-256 of `packet.json` equals the plan's;
- the running executable has the plan's size and SHA-256 (packet versions 3
  and 4: 65,503,104 bytes, `55f57947…c36`, the file rounds 25 and 26 measured);
- the bench plugin in `Data` has the plan's size and SHA-256;
- the loaded full and light plugins equal the plan's list, in order;
- every part file exists in the game folder, has the plan's SHA-256, has 1–16
  lines, every line passes the closed list, exactly one `ospsnap`, the planned
  number of `Screenshot` lines, and a part with `Screenshot` holds only
  `Screenshot` and `ospsnap`;
- `layout`: following `tm` and `tfc` through all parts in typed order, every
  `Screenshot` finds the interface hidden and the free camera on, and the last
  part leaves both as they were.

It also records, without refusing: whether the player is already in a cell (a
save was loaded), whether `GameDaysPassed`/`TimeScale` were found, the INI keys
of the packet (each an exact setting name, with or without a `:Section`; a key
not found carries its reason and the settings whose names differ from it only
in case or section, as evidence; those never count as the key), hashes of its own DLL and the F4SE runtime DLL, Windows/Wine versions,
the primary display adapter, the window size, and how long hashing the DLL,
hashing the executable and the whole start took.

On later marks:

| Part | Check | On failure |
| --- | --- | --- |
| cell | the player's parent cell is the bench cell (bench load index × 2²⁴ + local id) | abort |
| pose | starts the settle clocks: "command sent"; records whether the pose is already in effect | — |
| settle | the player stands at another step's pose | abort (parts out of order) |
| settle | the pose is not in effect yet: the first time, timing restarts at this mark | "not in effect", same part again |
| settle | wall-clock and simulated seconds since the timing origin are both at least `settle_seconds` (simulated is skipped, and recorded as null, if the globals are missing) | "not settled", same part again |
| settle | still not at the pose after a restart and a full wait | warning; recorded; the step will not qualify |
| capture | the camera is readable, complete and in the free state (3) | abort |
| capture | the free camera was held at least `free_camera_seconds` | recorded |
| photo, repeat, restore | the camera is read and recorded | — |
| any | a `PreSaveGame` message; a load or new game after the cell was confirmed | abort |

Why settling is timed from the pose mark: in round 25 the pose commands took
effect only after their part file had run, so the pose mark always saw the
previous position (`pose_observed_at_pose_mark` was false in all four steps),
the old clock started at the first settle mark at 0.0 s, and every step needed
the settle part twice. Measured from the pose mark, round 25's first settle
marks came 72–82 s and 52–57 game seconds later and would all have been
accepted (the native test replays step 1 with its recorded words). The timing
origin moves to a settle mark only when that mark does not see the pose: then
the pose command may have been sent by that very file (the settle part typed
before the pose part), so timing from it is the safe reading. Waiting with the
console open still fails, because game time does not advance then.

Each mark records in `observed` what it saw: `pose_in_effect_at_pose_mark` at
the pose mark, `pose_in_effect` and `timing_origin` (mark index and role) at
each settle mark, and `timing_restarted` when the origin moved.

A mark arriving after the run has ended changes nothing and says so.

## 5. What the plugin writes

Only `Data\F4SE\Plugins\osp_measure\runs\<packet id>-<session>\run.json`,
rewritten (temporary file, flush, replace) after every mark and after every
F4SE message once the run exists. Folder and file names pass
`osp::seq::child`, which accepts single components of `[A-Za-z0-9_.-]` only, so
no write can leave `osp_measure\runs`. The run file holds no absolute path.
Screenshots stay where the game writes them; the plugin only reads them to hash
and bind them, and records at each mark any file it left for later (empty, or
still open for writing). Format: [result.md](result.md) (run schema 2).

While a packet is installed the plugin also journals every F4SE message it
receives (type, SDK name, data length, time, run status), from load onwards, in
`run.json`'s `f4se_messages`.

Pre-existing writes remain outside that folder: at load the plugin, as in
v5–v8, creates `Documents\My Games\Fallout4\F4SE\osp-measure\` and appends its
load and registration lines to `osp_measure.log` there; F4SE itself rewrites
its own logs in `Documents\My Games\Fallout4\F4SE\` at every launch. While a
packet is installed no legacy snapshot or dump is written. A restoration check
after a run should therefore expect exactly those Documents changes and no
others.

## 6. Windows and Mac/Wine

The plugin uses only the F4SE interfaces and Win32 calls listed in
provenance.md, so the same DLL serves both. Under Wine the person types the
same lines into the same console. `system.wine_version` records Wine when its
`ntdll` exports `wine_get_version`. Paths are derived from the running modules,
never from the working folder.

### 6.1 Observed in round 25 (Wine 10.0 on macOS, packet version 2)

These are observations of one run, not guarantees. The reference run's
environment, as the plugin recorded it: Windows version 10.0.19043 reported by Wine 10.0, adapter
"Apple M5", window client area 1280×720, executable 65,503,104 bytes with
SHA-256 `55f57947db9e05575122fae1088f0b0247442f11e566b56036caa0ac93329c36`.

- `bat <part>` read `<part>.txt` from the game folder (where `Fallout4.exe`
  is) and ran it; the 24 part hashes matched.
- **Screenshot location and name**: the game folder itself, as
  `ScreenShot<N>.png` with N counting from 0 (`iScreenShotIndex` was 0 at the
  start), one file per `Screenshot` line, ScreenShot0 to ScreenShot7.
- **Format and size**: PNG, 1280×720, RGB, equal to the window client size;
  files of 333,748 to 419,215 bytes.
- **Timing**: each picture was taken after its part file had finished running,
  with whatever interface state the file had left; each file was seen and
  bound at the next mark, none was found unfinished, none was pending at the
  end. Whether the plugin would ever meet a file still being written was not
  forced; v9 now records any file it leaves for later.
- The console echoes each typed line as `> <line>`, `Screenshot` answers
  `Taking screenshot.`, `tm` answers `Menus -> On` or `Off`, `tcl` answers
  `Collision -> Off` or `On`.
- Blind typing while `tm` hides the interface was accepted in all four steps,
  and the restoring `tm` brought the console back.
- `setpos` and `setangle` took effect only after their part file had run.
  Negative player Z (−113.31) held at the requested value.
- With the console open, `GameDaysPassed` did not change (25.9 s between the
  cell and pose marks, identical words); `coc` from the main menu reset it
  (1.4583 days at the menu, 0.4643 after the load). `TimeScale` was 20.
- The free camera reported state 3 at every capture after a 3 s wait with the
  console closed, and capture and repeat camera words were identical.

### 6.2 Observed in round 26 (Wine 10.0 on macOS, packet version 3)

Again one run's observations, not guarantees, in the same reference-run
environment as round 25 (Windows 10.0.19043 reported by Wine 10.0, adapter "Apple M5",
client area 1280×720, the same executable), with the build `ecc774b6…`.
The run reached `complete`: 32 marks, eight screenshots bound, none pending,
none unexpected, 806 s (13 min 26 s) from the first to the last mark.

- **Setting names as found** (exact-name lookup): the antialiasing setting
  only as `SAntiAliasing:Display` (prefs, string, exactly `FXAA`), while the
  INI file held `sAntiAliasing=FXAA` under `[Display]`; `sAntiAliasing:Display`
  was not found. Round 25, with the same lookup and `TAA` set, had found
  `sAntiAliasing:Display`; the cause of the difference is unknown. The
  screenshot base name as `sScreenShotBaseName:Display` = `ScreenShot` (ini);
  the four autosave settings as `bSaveOnTravel`, `bSaveOnWait`, `bSaveOnRest`
  and `bSaveOnPause`, without a section (prefs, all `false`: autosave had been
  switched off before the run); no setting at all near `bAllowScreenShot:Display`.
  Eleven of the 21 requested names were found exactly. Packet version 4 asks
  for the names as found ([packet.md](packet.md#settings-names-as-found-and-what-decides)).
- **F4SE messages**: four, all at load and before the run existed:
  `PostLoad` and `PostPostLoad` at 0.003–0.004 s, `GameDataReady` (data
  length 1) and `GameLoaded` at 6.71 s. None arrived at the main menu, between
  the start mark and the mark before `coc`, around `coc` or later; no save,
  `NewGame` or `PostLoadGame` message. The journal's write from inside the
  message callback once a run exists was therefore not exercised.
- **Start timings**: hashing the DLL 0.014 s, the executable 0.239 s, the
  whole start 0.298 s.
- **Settling**: `pose_in_effect_at_pose_mark` false at all four pose marks
  (as in round 25). After a 45 s wait with the console closed, every settle
  part was accepted the first time, timed from its pose mark: 76.9–82.0 s of
  clock and 56.5–59.3 s of game time. `coc` from the main menu again reset
  `GameDaysPassed` (1.4583 days at the menu, 0.4656 after the load);
  `TimeScale` was 20.
- **Free camera**: state 3 at every capture, photograph and repeat after a
  3.2 s wait with the console closed (23–25 s from the free-camera mark to the
  capture mark, typing included); state 0 recorded at every restore part.
- **Blind parts**: the photograph, repeat and restore parts, typed while `tm`
  hid the interface, were accepted in all four steps, and after each restore
  part the console was visible again with `Menus -> On`.
- **Photograph and repeat**: 10.5–11.5 s apart; `GameDaysPassed` had the same
  bits at both marks in every step, so game time did not advance between
  them; the camera words were identical at capture, photograph and repeat.
  Each file was found and bound at the mark after its `Screenshot` part; no
  file was ever left for later. All four pairs passed the photo/repeat
  agreement check (largest statistic difference 0.17 levels, in `sign_band`
  of the first step; no pixel over 64), and none of the eight images shows
  console, compass or cursor.
- **Console**: every `osp:` message on its own line, the last one
  `osp: next: bat <part>`; a long message wraps on screen without running into
  the next.
- **Afterwards**: in `Documents\My Games\Fallout4\F4SE\` only F4SE's own logs
  and the appended `osp_measure.log` lines changed.
- **Result**: `supported` on the numbers, every step qualified, every
  requirement met except antialiasing, which was unverified (name not found,
  text unpinned), so not admissible.
- A second opening confirmed the two negative cases on purpose: a settle part
  typed before its pose part restarted the timing (`timing_restarted`), and a
  wait with the console open was refused (67.4 s of clock, 0.0 s of game
  time).

## 7. Offline analysis

```sh
python3 tools/osp_packet.py analyze <run-folder> --packet <packet.json> \
    --screenshots <game folder or a copy of its ScreenShot files> --game-dir <game folder> \
    [--out <result.json elsewhere>] [--retrospective]
python3 tools/osp_packet.py check <result.json> --packet <packet.json>
```

`analyze` reads `run.json`, never modifies the screenshots and writes only
`result.json` in the run folder, or at `--out` (it refuses to overwrite a file
there), so stored evidence can be analysed without being touched. A packet with
a retired template (version 2) can still be analysed. Besides the criterion,
the analysis now requires photograph and repeat to agree inside the packet's
`image_checks` crops ([result.md](result.md)): on round 25's stored screenshots
this rejects all four repeats, which the version 2 analysis had qualified.
`--retrospective` gives any analysis the kind `retrospective`, never
admissible, for a stored run
read against a packet it was not made with. Round 26's run (version 3) read
against version 4 this way is `void` ("run was made with a different
packet"), its criterion `supported`, and not admissible for three reasons: the
kind, antialiasing unverified (the run recorded `SAntiAliasing:Display` only
as a candidate of the name version 3 asked for) and, without `--game-dir`,
`data_unmodded` unverified.

## 8. What code cannot enforce

- What the person types outside part files. The plugin sees only its own
  marks; a hand-typed command between marks is invisible unless its effect
  shows in the position, cell or camera checks.
- Which part file produced a mark. Every part ends in the same `ospsnap`; the
  plugin assumes the part it expects. A pose part typed twice instead of its
  settle part is read as the settle mark and its second `setpos` restarts the
  player's settling unseen; the settle-before-pose case is caught only because
  the pose is then not in effect at the next mark.
- That a part file's lines ran: `bat` runs them, the plugin only verified their
  bytes at start. A refused settle mark cannot stop lines after it in the same
  file; that is why only side-effect-free parts are ever refused.
- That the game never saves: the plugin issues nothing and the closed list has
  no save command, but the game may autosave on its own; the plugin detects
  `PreSaveGame` and aborts the run, it cannot prevent the save.
- Whether `tm` took effect: no published call reports menu visibility. The
  layout guarantees the order of commands across files; the photo/repeat
  agreement check catches an interface present in only one of the two shots,
  inside the listed crops. An interface present in both shots alike, or
  outside every listed crop, or smaller than the pixel threshold allows, is not
  caught.
- Frames: none are counted (section 1.2).
- Compatibility between a build and a packet is decided by the plan format
  (now 2), which old and new builds refuse from each other; within format 2,
  a build older than packet version 4 refuses a plan with a setting name
  without a section ("bad ini line"), and every plan it accepted is still
  accepted; `plugin_version`
  stays 9 because this document gives no rule for raising it, and `run.json`
  identifies the exact build by the SHA-256 of `osp_measure.dll`.
