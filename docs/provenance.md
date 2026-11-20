# Knowledge and ABI provenance

This file records where every game fact used by the plugin and the tools in
this repository comes from. The facts come from the official F4SE 0.6.23
source package, read at build time from an external copy whose files are
pinned by SHA-256 in `tools/sdk-sha256.json`. Paths in the tables are relative
to `src/f4se/` in that package. Version 7 additionally uses the permissive
layout facts documented, with their licence, in its own section. No game
executable was opened, scanned, disassembled, or decompiled. No Address
Library or Nexus download is used, and no CommonLib implementation is built,
included or linked. Records kept for tools that are not part of this
repository are not reproduced here.

## Used addresses and layouts

| Fact used | Published source | How it is used |
| --- | --- | --- |
| `F4SEInterface`, `PluginInfo`, messaging version 1, callback signature, Query/Load exports | `f4se/PluginAPI.h` | Header included directly from the external SDK; GCC compiles only these plain interface records. ABI sizes/offsets are asserted. |
| Runtime `0x010A0A30`, F4SE packed `0x00060170`, release 25 | `f4se_common/f4se_version.h` | Compile-time checks and strict acceptance gate. Packed game version is reported as `1.10.163.0`. |
| `g_dataHandler` RVA `0x058CF080` | Declaration: `f4se/GameData.h:308`; definition: `f4se/GameData.cpp:4` | Read the pointer at executable module base + RVA. **The address is in the SDK .cpp, not the extern declaration in the header.** |
| `g_player` RVA `0x05AA4388` | Declaration: `f4se/GameReferences.h:522`; definition: `f4se/GameReferences.cpp:6` | Same relocation rule; read only. |
| `g_gameSettings` RVA `0x058E1030` | `f4se/GameSettings.h:138`, definition `f4se/GameSettings.cpp:11` | Read singleton pointer at executable base + RVA. |
| SettingCollectionMap collectionMap `+0x128` | `f4se/GameSettings.h:66–85`, field at line 80 | Read only the published pointer; never inspect any map-internal fields. |
| CollectionMap::GetSetting RVA `0x0001E290`, void result, arguments BSFixedString* and Setting*** | `f4se/GameSettings.h:71–84`, address/signature at line 75 | Explicit this plus two argument pointers, invoked through the guarded void2 bridge. |
| BSFixedString is StringCache::Ref, containing one Entry* at offset 0 | `f4se/GameTypes.h:140–146,185` | Independently declared eight-byte POD key. No GCC SDK class object. |
| StringCache::Ref ctor RVA `0x01B41D40`, Ref*(const char*) | `f4se/GameTypes.h:146`; wrapper `f4se/GameTypes.cpp:13–16` | Engine constructor called with the POD's address as explicit this; returned pointer is unused, initialized key.entry is checked. |
| StringCache::Ref Release RVA `0x01B42FD0`, void() | `f4se/GameTypes.h:153`; wrapper `f4se/GameTypes.cpp:23–26`, automatic lifetime `GameTypes.h:188–197` | Release every successfully constructed key, including failed lookups. No manual refcount/header manipulation. |
| Setting data `+8`, name `+0x10`, size `0x18`, scalar/char* union | `f4se/GameSettings.h:6–39` | Checked byte reads and current string text. Type rules from `GameSettings.cpp:13–48`; u is explicitly unknown at line 23. |
| `tArray` fields: entries `+0`, capacity `+8`, count `+0x10`, size `0x18`; `UnkFormArray` is array of `TESForm*` | `f4se/GameTypes.h:229–236,441–442` | Explicit POD view; validate count/capacity, upper bound and pointer arithmetic. |
| DataHandler form arrays start `+0x68`; 159 consecutive arrays; AVIF index 98, GMST index 3 | `f4se/GameData.h:113–281` | AVIF array derived offset: `0x68 + 98*0x18 = 0x998`. GMST is **not** used as a surrogate for the settings registry. |
| DataHandler modList `+0xFB0`; loadedMods `+0x10`, lightMods `+0x28` within ModList | `f4se/GameData.h:105–111,294` | Full mod array at `+0xFC0`, light mod array at `+0xFD8`; combine and deduplicate identical pointers. |
| ModInfo name `+0x70` (260 bytes), recordFlags `+0x334`, modIndex `+0x370`, lightIndex `+0x372`; ESL flag `1<<9` | `f4se/GameData.h:29–101` | Report loaded ESM/ESP/ESL records; preserve load-array order. |
| TESForm size `0x20`, formID `+0x14`, formType `+0x1A`; NPC type 45, AVIF type 98 | `f4se/GameForms.h` TESForm fields and form type enum | Check types before foreign calls. FormIDs are eight hexadecimal characters without `0x`. |
| TESForm GetEditorID vtable slot `0x3A` | `f4se/GameForms.h`, between Unk_39 and Unk_3B | Read the MSVC vtable entry directly, call as `const char*(void*)`, and copy its returned string through checked reads. Missing EditorID becomes null plus an error. |
| Reference ActorValueOwner `+0x58`, baseForm `+0xE0` | `f4se/GameReferences.h:195,214` | Use the **subobject pointer**, not the player object's primary vtable, for AV getters. |
| ActorValueOwner GetValue slot 1, GetMaximum slot 2, GetBase slot 3 | `f4se/GameFormComponents.h:144–160` (GetMaximum at 150) | MSVC has one deleting destructor slot 0. Call type: `float(void* owner, void* avif)`. Never instantiate a GCC version of this class. |
| TESActorBase actorData `+0x68` | `f4se/GameObjects.h:93–107` | Read player baseForm, require NPC type, use baseForm + 0x68 as GetLevel's `this`. |
| TESActorBaseData::GetLevel RVA `0x001497F0`, result UInt16 | `f4se/GameFormComponents.h:224` | Direct Microsoft x64 member-function call with explicit `this`. This is the SDK's base-data accessor; `level_source` states the exact source. In-game equivalence to the console remains to be checked in the game. |
| PostLoadGame type 3, NewGame type 8 | `f4se/PluginAPI.h`, messaging enum | Capture synchronously on each successful saved-game load or new-game creation. Ignore once-per-process GameLoaded. |
| PostLoadGame's bool is encoded as pointer value 0/1, dataLen 1 | `f4se/Hooks_SaveLoad.cpp:44–53` | Do not dereference `data`. Failed loads produce a log line and no snapshot. This implementation detail resolves ambiguous wording in PluginAPI.h. |
| NewGame precedes completion of loading | `f4se/PluginAPI.h` NewGame comment; `f4se/Hooks_SaveLoad.cpp:69–76` | Snapshot explicitly records this event phase. No guessed readiness address, delay, hook, or retry loop is added. Values are observations at that event, not a claim that initialization has finished. |
| Direct console table RVA `0x03706DC0`, direct script table RVA `0x036F6DD0` | `f4se/ObScript.h:49–50`; definitions `f4se/ObScript.cpp:4,6` | These are arrays of records at the RVA, not pointer-to-singleton locations. |
| Command counts `0x0209` console, `0x0332` script; console opcode base `0x0100` | `f4se/ObScript.h:40–47` | Bounded scanning/catalog; opcode-range check before registration. |
| ObScriptCommand size `0x50`, names +0/+8, opcode +0x10, help +0x18, parent +0x20, count +0x22, params +0x28, execute +0x30, parse +0x38, eval +0x40, flags +0x48 | `f4se/ObScript.h:18–38` | Independent POD, asserted size/key offsets. Preserve opcode/parser; zero argument metadata and replace only the unused command entry. |
| ObScript Execute bool callback with eight parameters, result at seventh argument | `f4se/ObScript.h:15` | Plain Microsoft x64 callback; no scriptData decoding. Optional result written through checked Windows API. |
| SDK names ToggleESRAM as an unused slot candidate; F4SE uses ForceRSXCrash itself | `f4se/Hooks_ObScript.cpp:8–20` | Match the current unused name after PostLoad; do not overwrite an already renamed slot. |
| F4SE's own registration strategy copies one command record and changes metadata/execute | `f4se/Hooks_ObScript.cpp:30–53` | Independent implementation uses VirtualProtect/WriteProcessMemory on the table data record and restores page protection. No F4SE helper implementation copied. |

Line numbers refer to the supplied 0.6.23 package, not the current online branch.
The singleton addresses have only extern declarations in headers; their
published SDK definitions above are necessary. They are not newly discovered
addresses. The byte views are independently written declarations of these facts.

## Published name lookup, without map enumeration

`f4se/GameSettings.h:66–85` publishes `SettingCollectionMap::collectionMap` at
`+0x128` and a name lookup function at RVA `0x0001E290`. The nested
`CollectionMap` has no fields, base class, count, bucket layout, or iterator.
Neither `PluginAPI.h` nor the other package headers provides enumeration.
`g_gameSettings` is declared in `GameSettings.h:138` and defined at RVA
`0x058E1030` in `GameSettings.cpp:11`. These facts do not provide enumeration,
but they do provide the complete call contract for lookup by name.

`GameTypes.h` contains generic containers, but nothing in GameSettings.h ties
this opaque map to any one of them. Casting it to a chosen map would invent
game knowledge beyond the authorized source. `DataHandler::arrGMST` is a form
array, not documented as all registered settings, and has no published GMST
object-to-Setting layout. Reading ESM defaults or querying a fixed list of names
also cannot establish that all runtime settings have been observed.

Coverage is therefore the names listed in a runtime text file. The DLL
consults those names and states configured_names explicitly. It never claims
complete enumeration of the engine registry. Source hashes pin the SDK inputs
consulted; no setting names or values taken from game files are compiled into
the DLL or committed.

`GetGameSetting(const char*)` in `GameSettings.h:141` is an SDK helper, not a
published address of a game free function or a plugin API export. Its body in
`GameSettings.cpp:110–116` shows the public path: construct an auto fixed-string
key, call the collection's Get wrapper, then release the key on scope exit.
Get itself in `GameSettings.h:82–84` delegates to the address at line 75 using
collectionMap as this. Passing that opaque pointer to the published function
does not require reconstructing its container.

The call arguments are explicit this=map, RDX=&fixed_string, R8=&output_cursor.
There is an initially null eight-byte Setting* output slot; output_cursor
initially points to it. The caller uses exactly the output-slot contract shown
by the SDK helper, including its Setting*** argument. It rejects an unexpected
cursor rebind instead of guessing another return layout. The find function's
return register is ignored because the published return type is void.

The key is an eight-byte POD with one pointer, initialized by the published
engine constructor and balanced by the published Release call. GetSetting is
the only operation on map internals. Constructor/find/release use Microsoft
x64 calls through SEH. They run on the same F4SE callback thread as the snapshot.
Temporary string-cache references are the normal lookup mechanism; no setting
or actor value is changed. No F4SE helper implementation is copied or linked.

The decoder uses b/i/f/s/S/r/a rules from `GameSettings.cpp:13–48`, including
low-byte bool, signed int32, float32, current char* string, ID6 shift-eight and
UInt32 ID. Since that source explicitly marks u unknown, the runtime emits
type=unknown, value_encoding=raw_u32, current Data.u32 and a diagnostic. It does
not infer an unsigned semantic type from the name. The comparison tool may
compare those raw bits numerically with an exported file integer, disclosing
the basis. English .STRINGS files are not provided/decoded; strings are marked
not compared in that tool while current runtime text is still captured.

## Native plugin registration versus loaded game plugins

The public API exposes `GetPluginInfo(name)` only; it has no iterator of registered
native plugins and no mapping from DLL filenames to registration names.
`plugins.items` therefore means loaded game ESM/ESP/ESL files from DataHandler.
Separately, Windows Toolhelp enumerates resident modules and GetProcAddress tests
the two plugin exports. `native_plugin_modules.items` reports those modules,
with paths and export presence. This does not prove successful F4SE registration;
that limitation is explicit in the JSON. No exports are called during discovery.

## Console, sequencing and hit-event limits

`ObScript.h` publishes opaque scriptData/opcodeOffset pointers but no argument
extraction function, string parameter encoding, compiler, or text execution API.
`Hooks_ObScript.cpp:30–50` demonstrates a no-argument handler. Version 3 therefore
implements ospsnap without console arguments, with optional tags read from the
atomic Documents control file. Literal `ospsnap <tag>` is not implemented.

`PluginAPI.h` publishes AddTask, and `GameThreads.h:3–9` describes task callbacks,
but these do not provide command parsing/execution. `Hooks_Threads.cpp:24–45`
drains the queued tasks immediately; re-queuing from a task is not a published
frame-delay mechanism. There is no native command runner: waits and console
input belong to the person (or a tool) typing at the console.

`GameEvents.h:53–56` leaves TESHitEvent empty. Its published dispatcher list at
226 onward has combat/death/furniture/load/object/init events, no hit dispatcher.
`PluginAPI.h:122–124` states that the messaging event-dispatcher accessor has no
implemented dispatchers. The player hit sink field in GameReferences.h is not a
dispatcher registration API or hit payload contract. No guessed sink insertion,
hook, damage layout or invented hit record is used. Capabilities explicitly
report hit_events=false.

The SDK hash manifest pins these console, event and task sources with the
others. The version/runtime gate is unchanged.

## Compiler and fault boundary

MinGW-w64 x86-64 uses Microsoft x64 register/stack calling conventions for the
plain function pointers here. It does **not** implement the MSVC C++ object ABI.
The plugin uses no game class inheritance, virtual-call syntax, RTTI casts,
member-function-pointer casts, GCC instantiation of SDK constructors, or STL
objects across the boundary. It calls only the published engine fixed-string
constructor/release for temporary lookup keys. Fields are copied with
ReadProcessMemory into POD views;
all used offsets have static assertions. API records contain plain values and
function pointers and are included from PluginAPI.h directly.

`src/foreign_call.S` supplies three small, original wrappers with an ordinary
x64 prologue, 32 bytes of shadow space, aligned stack, and `.seh_*` unwind data.
Their foreign-call instruction is inside a scope table handled by the Windows
CRT `__C_specific_handler`. Hardware faults from getters return failure to the
caller; no process-wide exception handler is installed. GCC C++ try/catch alone
would not provide this protection. No C++ object requires destruction in the
assembly frame. Catching a getter fault does not repair a corrupt game; fail-fast
termination or an unrelated game thread's crash cannot be prevented by this DLL.

The DLL statically links the MinGW/GCC C++ runtime. PE inspection enforces only
system DLL imports. The GCC Runtime Library Exception applies to compiler runtime
components; it does not replace this project's dual license or F4SE terms.
No clang-cl, Microsoft SDK libraries, xwin, or Microsoft license acceptance is
needed for this build. SEH runtime behavior still needs a Windows/Wine test;
host tests cover cross-compilation and structural verification of the lookup
calls. Version 1 produced real captures with 597 ActorValues in the game; that
does not validate the later lookup path.

## F4SE terms read from the package

Both `f4se_readme.txt` at package root and `src/f4se/f4se_readme.txt` were read.
They explicitly reject redistributing or making modified versions of F4SE,
recommend extending it with a plugin, and require plugin source to be publicly
available. They provide an as-is warranty/liability disclaimer. No general
MIT/Apache grant for F4SE was found in the supplied src tree. The bundled xbyak
has its own license; this project does not use it.

This repository does not vendor SDK files, compile F4SE .cpp files, redistribute
F4SE binaries, or claim a license over them. It includes PluginAPI.h and the
version macros from an external copy of the package at build time. Original
measurement logic is MIT OR Apache-2.0. This repository is the plugin's public
source, as the F4SE readme requires of plugins; a release ships only
`osp_measure.dll`. Current upstream corroboration (the 0.6.23 readmes control
this build): [F4SE team's readme](https://github.com/ianpatt/f4se/blob/master/f4se_readme.txt).

## Version 4: light capture

Schema 4 and light capture add no game address, field layout or virtual slot.
Full captures retain the tables above; light captures use only the existing
DataHandler AVIF array, player/base-form level and ActorValueOwner/getter paths.
Settings lookup/key construction/release, mod arrays, Toolhelp module enumeration
and console catalogue traversal are skipped in light mode. The registered
zero-argument command and all calling conventions are unchanged.

## Version 5: equipped weapon instance (read-only additive extension)

All paths below are in the same pinned external F4SE 0.6.23 SDK. No new engine
function, virtual call, hook, address or GCC SDK-class object is introduced.
Only independent POD views and checked ReadProcessMemory reads are used.
The existing player singleton and TESForm byte layout are reused.

| Layout used | Published source | Application |
|---|---|---|
| Actor.middleProcess +0x300, MiddleProcess.unk08 +8, Data08.equipData +0x288 | GameReferences.h:390-435,476 | Read tArray of equipment entries; exactly one type-WEAP item required. |
| EquipData item +0, instanceData +8, equipSlot +0x10, equippedData +0x20; stride 0x28 | GameReferences.h:401-410 | Stride derived from declared fields and default x64 alignment; no opaque fields interpreted. |
| WEAP form type 43 | GameForms.h:214-258 FormType enum | Check the item before reading its weapon instance. |
| Actor.equipData +0x428; PlayerCharacter.playerEquipData +0xB70 | GameReferences.h:457,474,515,525; PapyrusActor.cpp:40-47 | Find corroborating same-item/same-instance mod-extra pointers in both published equipment views. |
| ActorEquipData slots +0x10, 44 slots, SlotData stride 0x58; item +0, instanceData +8, extraData +0x10 | GameFormComponents.h:1423-1452 | Slots offset derives from the preceding two eight-byte fields. PapyrusInstanceData.cpp:75-94 confirms instance ownership through the slot. |
| TESObjectWEAP::InstanceData size 0x138; ammo +0x68, aimModel +0x88, zoomData +0x90 | GameObjects.h:277-297,373 | Report referenced FormIDs; never replace missing instance with weapData. |
| reloadSpeed +0xC4, speed +0xC8, minRange +0xD0, maxRange +0xD4, attackDelay +0xD8, outOfRangeMultiplier +0xE0 | GameObjects.h:319-327 | float32 checked for finiteness. |
| flags +0x110; Automatic 0x4000, BoltAction 0x400000 | GameObjects.h:340-362 | These are SDK mask tests. The runtime semantic mapping is not independently verified; retain raw bits. |
| ammoCapacity uint16 +0x130, baseDamage uint16 +0x132, accuracyBonus uint8 +0x136 | GameObjects.h:368-372 | Preserve declared types; no inference of displayed damage. |
| TESObjectREFR.inventoryList +0xF8; BGSInventoryList.items +0x58 | GameReferences.h:234; GameFormComponents.h:1516-1525 | Read-only fallback for equipped object mod metadata. |
| InventoryItem stride 0x10 (form +0, stack +8); Stack next +0x10, extraData +0x18, equipped mask 7 at +0x24 | GameFormComponents.h:1474-1513; GameReferences.cpp:16-61 | Same published traversal criterion, independently implemented with bounds and cycle detection; no SDK implementation copied. |
| ExtraDataList.m_data +8; BSExtraData.next +8/type uint8 +0x12; ObjectInstance type 0x35 | GameExtraData.h:65,229-272 | Bounded linked-list reads; no allocations or mutations in engine memory. |
| BGSObjectInstanceExtra.data +0x18; Data.forms +0, blockSize uint32 +8, Form stride 8/FormID +0 | GameExtraData.h:275-294; PapyrusActor.cpp:59-87 | blockSize offset +8 derives from its declaration after an eight-byte pointer; the SDK comment saying +04 contradicts that declaration. No packing directive applies. Length=blockSize/8; reject nonmultiples and excessive lengths. |

The SDK only forward-declares BGSAimModel and BGSZoomData in
GameFormComponents.h:20-21; it does not publish their field layouts. A FormID
can be read through TESForm, but aim fields and zoom FOV remain explicitly
unavailable in the DLL. Unknown fields
in InstanceData::FiringData (GameObjects.h:299-312) are not renamed as Animation
Attack/Reload/Bolt Seconds. PapyrusInstanceData.cpp:16-106 also contains mutating
clone/add fallback paths; version 5 does not call or reproduce those mutations.

## Version 6: published stationary scene graph

These additions use the same external 0.6.23 SDK, with all seven new source
files pinned in `tools/sdk-sha256.json`. No SDK definitions are vendored or
instantiated as GCC game classes. `include/osp/nodes.hpp` is an independent
byte view and traversal. Sources below are relative to `src/f4se/f4se/`:

| Address, layout or virtual slot | Local SDK source | Use |
|---|---|---|
| LookupFormByID RVA 0x00152C90; UInt32 argument, TESForm* result | `GameForms.cpp:4`, `GameForms.h:36–37` | Resolve the requested FormID; reject non-REFR/ACHR before interpreting a reference layout. |
| Form types REFR=64, ACHR=65 | `GameForms.h:213–280`, sequential FormType enum starting at NONE=0 | Type gate; TESForm ID/type fields retain the existing published view. |
| TESObjectREFR baseForm +0xE0 | `GameReferences.h:214,249` | Base FormID. |
| GetActorRootNode slot 0x8B (bool firstPerson), GetObjectRootNode slot 0x8C | `GameReferences.h:128–129` | Player's two labelled roots and other reference object root. |
| Alternative loaded root path, not used | `GameReferences.h:217–233`: LoadedData.rootNode +8; unkF0 +0xF0 | Published corroboration only; the dump uses named getters. |
| NiObject GetRTTI slot 2, GetAsNiNode slot 3, GetAsBSGeometry slot 8 | `NiObjects.h:29–63` | Count from the MSVC single virtual destructor at slot 0 and DeleteThis at slot 1. Unk_05 confirms the slot sequence. No GCC vtable calls. |
| NiRTTI name +0, parent +8 | `NiRTTI.h:8–13` | Read the returned RTTI name only; no private cast/type table. |
| NiObjectNET m_name +0x10 | `NiObjects.h:104` | Existing BSFixedString pointer read; no construction/refcount mutation. |
| BSFixedString = StringCache::Ref; Entry state +8, length +0xC, externData +0x10, data +0x18; External=0x4000, Wide=0x8000 | `GameTypes.h:96–185`, Entry::Get/IsWide and Ref conversion | Follow bounded/cycle-checked external links; then read narrow bytes or UTF-16. The entry header is not character data. RTTI remains a direct char pointer. |
| NiAVObject m_parent +0x28, local +0x30, world +0x70 | `NiObjects.h:154–156` | Parent and two exact transform byte views. |
| NiTransform size 0x40: rot +0, pos +0x30, scale +0x3C | `NiTypes.h:319–331` | Preserve 16 float words. |
| NiMatrix43 float data[3][4], size 0x30 | `NiTypes.h:299–307` | Emit first three lanes per row as a 3×3 matrix; preserve the other three lanes as uninterpreted rotation padding. No composition convention assumed. |
| NiNode m_children +0x120 | `NiNodes.h:21` | Sparse ordered child traversal only after GetAsNiNode succeeds. |
| NiTArray pointer +8, capacity +0x10, emptyRunStart +0x12, size +0x14 | `NiTypes.h:340–357` | Iterate slots before emptyRunStart; skip null entries as instructed at lines 348–349. |
| BSGeometry skinInstance +0x140 | `BSGeometry.h:121` | Read only after named geometry getter succeeds. |
| BSSkin::Instance bones +0x10, worldTransforms +0x28 | `BSSkin.h:28–31` | Ordered bone names through the already published tArray layout. worldTransforms are not needed/read. |

The existing checked `osp_call_word` Microsoft x64/SEH bridge covers the
pointer getters and the UInt32 form lookup (RCX contains the ID for the latter;
an unused second argument is harmless in this ABI). The command does not
write node fields, change reference counts, construct SDK objects or write the
console result. Registration still patches only the SDK-named ToggleESRAM slot
(`Hooks_ObScript.cpp:8–19`); the new short-name alias shares that record. A
Documents sidecar carries the new FormID because ObScript.h has no published
extractor; no script bytecode layout is inferred.

Legacy snapshot readers and serialization remain pinned and byte-tested with
synthetic inputs. The catalogue hides only this dispatcher's short-name alias
to preserve v5 bytes. The old payload version is a compatibility version;
F4SE Query and separate node dumps identify the v6 binary.

## Version 7: permissive layout references and read-only camera

Before any code file was opened in each reference repository, its pinned
LICENSE was read in full. The selected source is
[LucaDotGit/CommonLibF4](https://github.com/LucaDotGit/CommonLibF4/tree/e337150ac993daaa9d089fcddf7a6ce9a6bad42f),
commit `e337150ac993daaa9d089fcddf7a6ce9a6bad42f`, whose README explicitly lists
OG 1.10.163.0. Its MIT license was read first. Ryan McKenzie and the CommonLibF4
contributors, including LucaDotGit, receive credit for published layout facts.
We neither build/link the library nor use its relocations, Address Library,
implementation algorithms or executable inspection. Reference files are never
in the source repository. Each layout is
an unverified candidate until all four prospective live proofs pass.

The complete license at
[LICENSE.md](https://github.com/LucaDotGit/CommonLibF4/blob/e337150ac993daaa9d089fcddf7a6ce9a6bad42f/LICENSE.md):

```text
MIT License

Copyright (c) 2019 ryan-rsm-mckenzie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The independently written `osp/scene.hpp` uses these facts (paths relative to
that pinned source unless the row says external F4SE):

| Fact | Source | Read-only use |
|---|---|---|
| BSFlattenedBoneTree count +0x140, expanded count +0x144, records pointer +0x148; size 0x180 | `include/RE/B/BSFlattenedBoneTree.hpp:52-57` | Bound counts independently to 2048; export boneCount records. No capacity or ordering relationship between the counters is published or assumed. Never inspect boneMap. |
| FlattenedBone size 0xA0; local +0, world +0x40, signed parent +0x80, child +0x82, count +0x84, sibling +0x86, NiNode pointer +0x88, fixed-string name +0x90, deferred flag +0x98 | same header `:19-34` | Checked independent byte view, parent-cycle checks, preserve raw floats and ordinary-node pointer for the equality proof. |
| NiCamera worldToCam 4x4 +0x120, frustum +0x160, viewport +0x184; size 0x1A0 | `include/RE/N/NiCamera.hpp:72-79` | Export exact words; infer no projection convention from field names. |
| NiTransform rotation +0, translation +0x30, scale +0x3C; size 0x40 | `include/RE/N/NiTransform.hpp:34-38`, corroborated by external F4SE NiTypes.h | Same byte encoding as schema 1. |
| g_playerCamera pointer RVA 0x058CEB28 | external F4SE `GameCamera.cpp:4` | Resolve the public camera singleton without a new third-party relocation. |
| TESCamera.cameraNode +0x20, cameraState +0x28; stateID +0x20; world FOV +0x168, first-person FOV +0x16C; free state enum 3 | external F4SE `GameCamera.h:25,62-63,82-96,116-117` | Traverse the published camera root with existing getters. Accept an active camera candidate only when exactly one NiCamera is found in the complete hierarchy; ambiguous roots remain incomplete. |
| NiFrustum left/right/top/bottom/near/far +0/+4/+8/+0xC/+0x10/+0x14, ortho +0x18; NiRect same first four fields | external F4SE `NiTypes.h:166-187` | Read the named frustum and viewport fields; no projection call or guessed matrix offset. |

The cited third-party line numbers were checked against the pinned copies.
The SDK camera files are pinned with the other external SDK inputs. Existing
NiObject virtual slots and StringCache representation retain the version 6 sources;
no additional engine virtual call is introduced. All values are read on the
console callback thread. The schema-2 command path writes only its output file,
never camera/node fields or the console result pointer. QPC remains observation
time, not simulation or animation time. Public camera/photo procedures contain
no game-derived scene names, poses, image pixels or asset lists.

Schema-2 window metadata uses public Microsoft Win32 queries only:
[GetClientRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect),
[ClientToScreen](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clienttoscreen),
and [GetWindowRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect).
Client bounds exclude the bottom/right edge. Window bounds may include invisible
resize borders and DPI virtualization, so they are not substituted for measured
Mac screenshot boundaries. The foreground-window process identity is checked;
no window is moved, resized, focused or modified by the plugin.

Permissive reference audit (every license was read before its code):

| Repository | Pinned commit | Result |
|---|---|---|
| [Ryan-rsm-McKenzie/CommonLibF4](https://github.com/Ryan-rsm-McKenzie/CommonLibF4) | `67173f8d44b9a928dbab983d25e576ac5f1e83ac` | MIT; earlier candidate; no flattened layout file found in its tree |
| [alandtse/CommonLibF4](https://github.com/alandtse/CommonLibF4) | `15f504eacd90df1e586767c02f21a5f329c3f821` | MIT; earlier candidate; no flattened layout file found in its tree |
| [Zzyxz/CommonLibF4RD](https://github.com/Zzyxz/CommonLibF4RD) | `b092bcc70f690b2c51b980c0b251fa56bf2d3460` | MIT; earlier candidate; no flattened layout file found in its tree |
| [powerof3/CommonLibF4](https://github.com/powerof3/CommonLibF4) | `7e99df6ba3395afbc59b6efa0ff56780a4199963` | MIT; earlier candidate; no flattened layout file found in its tree |
| [LucaDotGit/CommonLibF4](https://github.com/LucaDotGit/CommonLibF4) | `e337150ac993daaa9d089fcddf7a6ce9a6bad42f` | MIT; selected flattened/camera layout reference |

All five pinned licenses contain the identical MIT text and copyright notice
reproduced above. Copyleft candidates were excluded without opening code.

An early safety gate incorrectly required expanded >= count. That relationship
does not follow from this layout declaration. The plugin bounds each counter
independently and keeps both offsets and all record layouts unchanged. It is
not a scan or an offset inferred from memory.

## Version 8: schema-3 additions

No new third-party layout, relocation database or game implementation is used.
The same pinned MIT CommonLibF4 facts from version 7 describe the flattened
fields; its LICENSE was reread before those cached headers. Copyright remains
with 2019 Ryan McKenzie and the reference contributors. No reference code was
copied into the repository or linked.

| Fact | External source | Use |
|---|---|---|
| TESObjectREFR parentCell +0xB8, rot +0xC0, pos +0xD0 | F4SE 0.6.23 `f4se/GameReferences.h:209-214` (member comments and declarations; see checked local SDK pin) | Schema3 raw position/rotation words and parent CELL FormID; SDK comment “Probably quat?” prevents an invented angle interpretation. |
| FixedString constructor / Release and 8-byte Entry pointer | F4SE `GameTypes.h:142,146,153` (Entry, constructor/Release RVAs); wrappers `GameTypes.cpp:13-16,23-26` | POD key and guarded Microsoft x64 calls for prospective case-equivalence pointer probes, balanced before allocation. |
| Loaded full/light plugin arrays and names/indices | F4SE `GameData.h:105-111,294` and existing mod layout provenance | Future bench verification uses the full ready snapshot's `plugins` section, never a filename's implied index. |

FTZ describes underflowed *results*, whereas DAZ describes input denormals:
[Intel compiler documentation](https://www.intel.com/content/www/us/en/docs/fortran-compiler/developer-guide-reference/2023-1/ftz-qftz.html).
The project's two software predictions do not change the engine FP environment.
The separate preserved-subnormal prediction is always exported alongside FTZ.

Antialiasing: the `sAntiAliasing` key under `[Display]` and the values `TAA`
and `FXAA` appear in the game's shipped graphics presets. The
[Step project's INI reference](https://stepmodifications.org/wiki/Guide:Fallout4Prefs_INI/Display#sAntiAliasing)
documents an empty value as disabled; this is configuration reference material
only. A packet pins the exact setting name and value text that a run recorded
(`docs/packet.md`), never a name from these references.

[Bethesda Support](https://help.bethesda.net/app/answers/detail/a_id/33489/)
confirms the default `%LOCALAPPDATA%\Fallout4\plugins.txt` location.
[LOOT's own load-order documentation](https://loot.github.io/docs/help/introduction-to-load-orders/)
documents Fallout4's leading `*` activation marker and textual order. These are
public primary documentation sources; no LOOT/GPL implementation was read.
That this protocol applies to the pinned 1.10.163 runtime is checked by the
plugin's own loaded-plugin list in every run, not by disassembling the
executable.

## Version 9: stepped sequencer

The feasibility review is in [sequencer.md](sequencer.md), section 1: the
pinned package publishes no way to compile or run a console line and no
per-frame callback, so the plugin issues no command and counts no frame. Only
these facts are new. Each is an address or field already declared in the
official 0.6.23 package; `GameAPI.h` and `GameAPI.cpp` are added to the SDK
manifest (now 36 files). Byte views are in `include/osp/sequencer_layout.hpp`
with static assertions.

| Fact | Published source | Use |
| --- | --- | --- |
| `g_console` RVA `0x058E0AE0`, a `RelocPtr<ConsoleManager*>` | `f4se/GameAPI.h:26`, definition `GameAPI.cpp:17` | Read the manager pointer at image base + RVA; skip printing when null. |
| `ConsoleManager::Print` RVA `0x01262F50`, `void(const char*)` | `f4se/GameAPI.h:18–24` (line 23) | Explicit `this` plus one string through the existing guarded `osp_call_void2` bridge. Text is printable ASCII without `%`, because the call is not documented as format-free. F4SE's own handler prints from inside a console callback (`Hooks_ObScript.cpp:30–39`). |
| `g_iniSettings` RVA `0x05EDB528`, `g_iniPrefSettings` RVA `0x05B5BE58` | `f4se/GameSettings.h:135–136`, definitions `GameSettings.cpp:5,7` | Read each collection pointer. |
| `SettingCollectionList::data` (`Node*`) at `+0x120`; `Node {Setting* data; Node* next}` | `f4se/GameSettings.h:96–110` | Bounded, cycle-checked walk until a null `next`, as `GameSettings.cpp:119–137` does; exact name comparison (no fixed-string construction); INI list first, then prefs (`GameSettings.cpp:101–108`). Values decoded with the existing `Setting` view and type rules. |
| `TESGlobal`: `editorID` (`const char*`) `+0x20`, `value` (float) `+0x30` | `f4se/GameForms.h:1754–1763` | Read the raw value word of the globals named `GameDaysPassed` and `TimeScale`. The names are game data, not SDK facts; absence is recorded as null. |
| DataHandler `arrGLOB`, form type 11 | `f4se/GameData.h:134`; enum `GameForms.h:215–226` | Array 11 of the already used form-array block: offset `0x68 + 11*0x18 = 0x170`; entries are type-checked. |
| `TESObjectREFR::parentCell` `+0xB8`, `pos` `+0xD0` | `f4se/GameReferences.h:209–212` | Already used by schema 3; now read for the player. |
| `kMessage_PreLoadGame` = 2, `kMessage_PreSaveGame` = 4 | `f4se/PluginAPI.h:97–115` | A save, or a load/new game after the bench cell is confirmed, aborts the running packet. Asserted at compile time. |

Not used, although published: `MoveRefrToPosition` (`GameReferences.h:28–29`),
the task interface (`PluginAPI.h:195–205`) and every `ObScriptCommand` handler
pointer. Calling a handler would need an unpublished parameter encoding.

Windows APIs added: `FindFirstFileW`/`FindNextFileW` (screenshot names in the
game folder), `GetFileAttributesW`, `CreateDirectoryW`, `MoveFileExW` with
replace, `EnumDisplayDevicesW`, and `GetProcAddress` lookups of `RtlGetVersion`
and Wine's `wine_get_version` in the already loaded `ntdll.dll`. No new import
library; no network API. The SHA-256 used for files is an original
implementation of FIPS 180-4 (`include/osp/sha256.hpp`), tested against the
standard's vectors and Python's `hashlib`. Hashing the executable reads its
bytes only; nothing parses or searches it.

### Version 9 after round 25

The changes made after the first live run add no address, offset or virtual
slot. What they rely on:

| Fact | Source | Use |
| --- | --- | --- |
| F4SE message types in enum order: PostLoad 0 … GameDataReady 10 | `f4se/PluginAPI.h:97–115` | Names in the run's journal of received messages. Type numbers outside the enum are recorded as `unknown`. |
| String settings start with `s` (static) or `S` (dynamic); `SetString` rewrites a static name's first letter to `S` | `f4se/GameSettings.cpp:25–26`, `77–92` (line 90) | Why a key not found is recorded with candidates whose names differ only in case: the same setting may carry either first letter. Candidates are evidence only; the plugin still finds a key by its exact name. |
| `ConsoleManager::Print` does not end the line | Round 25 observation (console screenshots in the round's stored evidence), not an SDK fact | Each message is passed with a trailing newline. The next run must confirm one line per message. |
| `setpos`/`setangle` and `Screenshot` take effect after their `bat` file has run | Round 25 observation, not an SDK fact | Settle timing starts at the pose mark; a `Screenshot` part holds nothing else. |

Windows calls added: none. `QueryPerformanceCounter` (start timings) and
`InterlockedIncrement` (count of messages not journaled) were already used.
