# Scene dump schema 2 (plugin v7)

Developed with AI assistance from GPT-6.1 Sol (OpenAI). Original code is
MIT OR Apache-2.0. External SDK/reference terms remain separate; see provenance.

Write a tag and `nodes2=00000014` into the existing Documents `control.tag`
atomically, then enter `ospnodes` without arguments. The alias and dispatcher
are unchanged. `nodes=` still produces the exact v6 schema-1 payload, and old
snapshot requests still produce the v5 compatibility payload. Schema 2 uses
its own counter and `<session>-scene-<counter>.json` filename. Atomic file
publication and read-only callback/thread restrictions remain unchanged.

Schema 2 extends the schema-1 envelope/tree fields, with `node_dump_schema: 2`
and `plugin_version: 7`. Each root additionally contains
`flattened_bone_trees`, selected only by the published RTTI name. Each candidate
exports its address, both named counts, array address, bound, errors and
`bones`: index, name, signed parent/child/sibling indices, child count, ordinary
node address, raw deferred-attach flag, local and world transforms. The
transform encoding is identical to schema 1, including separate padding.
Counts are bounded to 2048 per candidate, with at most eight trees and 4096
records per ordinary root. Aggregate truncation is explicit. Reads are checked
and parent cycles rejected. No
hash-map internals, undocumented arrays or instance-construction code are used.

`complete` means that the declared records were read without a diagnostic.
It does **not** verify the source layout. Every candidate explicitly states
`candidate_requires_four_external_proofs`; the envelope also preserves
`flattened_layout_validation: requires_four_external_proofs`. An external,
prospective file-backed proof must validate quantity/names, parent indices,
ordinary-node bit equality and internal parent/child composition, separately
for each skeleton. A failed proof prevents pose conclusions from those records.
The enclosing NIF header and the root of its flattened subtree are distinguished
in the frozen forecast. Root composition without an array parent is not guessed.
Equality covers the 13 semantic transform floats; padding is retained separately.

`camera` includes the SDK singleton and camera-state ID, raw world/first-person
FOV words, its root tree and the unique descendant with RTTI `NiCamera`.
Zero/multiple candidates or incomplete hierarchy leave it incomplete. The
selected camera exports world transform, 16 `world_to_camera_raw` scalars,
frustum `[left,right,top,bottom,near,far]`, orthographic flag and viewport
`[left,right,top,bottom]`. Every float preserves hex bits and exact finite
decimal, otherwise decimal null. Field names alone establish no axes, matrix
convention, FOV unit or pixel mapping; these require measured projection tests.

`window` is independent Win32 metadata: the foreground window must belong to
this plugin process. Client/window rectangles and client screen origin come
from public User32 queries. These coordinates do not by themselves establish
the Wine-to-Mac mapping, Retina scale, client image margin or render resolution.
Read the INI resolution keys and measure screenshot boundaries separately.

`tools/scene_dump.py` validates the inherited schema and extensions while
keeping unverified candidates explicit. `tools/bone_proofs.py` applies all four
proofs and withholds pose comparisons on any failure. `tools/camera_math.py`
enumerates prospective projection conventions; it performs no fitted camera
or image alignment. Game-specific forecasts, results and images remain ignored.

## Schema 3

Plugin8 accepts the additional `nodes3`/`pause` sidecar framing documented in
[round19-protocol.md](round19-protocol.md). It emits
`<session>-scene3-<counter>.json`, with an independent counter. Schema3 preserves
schema2 fields and adds proof evidence; schema2 retains its original status and
bytes. A schema3 root's flattened rows export original record local/world,
ordinary-node local/world, effective local/world and name Entry pointers.
`round19_proofs` records all four checks and every strict/FTZ prediction.
`round19_validation` identifies the forecast and accepted player pause IDs.
The envelope and per-tree status are `candidate_round19` or `validated_round19`.

`reference_sdk_pose` additionally preserves REFR position and uninterpreted
SDK rotation words plus parent CELL identity. This supports a future bench
cell check with the full snapshot's SDK loaded-plugin list. It is not an engine
EditorID lookup or an invented rotation-unit interpretation.

`tools/scene3_dump.py` strictly parses the inherited contract and independently
recomputes the new proofs against the frozen file forecast. A single capture's
native validated label is insufficient to establish a two-pause dataset proof.
The analyzer must match session, forecast and distinct protocol pauses first.
