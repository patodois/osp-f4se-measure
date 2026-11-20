# Stationary node dumps (plugin v6)

Developed with AI assistance from GPT-6.1 Sol (OpenAI). Original code is
MIT OR Apache-2.0; the external F4SE SDK keeps its separate terms.

## Request and console entry

With the console open, atomically replace `Documents\My Games\Fallout4\F4SE\osp-measure\control.tag`
with two ASCII lines (optional final newline):

```text
my-paused-pose
nodes=00000014
```

Then enter `ospnodes`, with **no console arguments**. The hexadecimal FormID
must have exactly eight digits and be nonzero; tags follow the existing bounded
tag alphabet. The SDK publishes no console argument extractor, so the FormID
is supplied by this sidecar. There is no script-data decoding or text runner.

`ospnodes` is the short-name alias on the same SDK-named `ToggleESRAM` slot as
`ospsnap`. Both names enter the same dispatcher: the new `nodes=` request dumps
nodes; an existing valid v5 request retains its previous snapshot behavior.
An alias is not a distinct opcode, so typing `ospsnap` with a `nodes=` request
also dumps nodes, and typing `ospnodes` with an old request produces a snapshot.
Always pair the documented request with its command; clear/reset the sidecar
before subsequent legacy snapshots. No second unused slot is guessed.

The old catalogue excludes the new alias, keeping the existing output bytes.
Legacy `plugin_version: 5` describes that compatibility payload; F4SE Query and
the separate node dump report plugin version 6. The node counter does not
advance the legacy sequence. No node traversal writes game memory, even the
console result pointer. Registration retains the existing console-table patch.

## JSON schema 1

Files are atomically published as `<session>-nodes-<counter>.json` in the same
Documents output directory, using a temporary file, flush and rename without
overwriting an existing dump. Header fields:

- `node_dump_schema: 1`, `plugin_version: 6`, `session_id`, `tag`;
- `requested_form_id`, `reference_form_id`, `base_form_id` (eight hex digits;
  unavailable observations are null);
- `dump_counter`, `windows_process_id`;
- `monotonic_ticks`, `monotonic_frequency`: QueryPerformanceCounter elapsed
  since plugin initialization in this process. This is not game animation time;
- `complete`, `errors`, `roots`.

The player has labelled `third_person` and `first_person` roots, obtained through
the published actor-root getter. Other readable REFR/ACHR forms use the published
object-root getter. A null/unloaded root is explicitly incomplete. A root label
identifies the getter, **not the active camera mode**. Each root contains its
getter slot and a `tree` with `root_address`, `complete`, `nodes`, `stops`, `limits`.

Nodes are depth-first in the SDK sparse child-slot order. Each has a contiguous
zero-based `index`, `parent_index` (null for traversal root), `child_slot`, `depth`,
`address`, `parent_address`, `name`, `type`, `kind` (`node`, `leaf`, `unknown`),
`local`, `world`, and `external_parent_world` for a root whose parent lies outside
the requested subtree. Names/type may be null when unreadable. RTTI and the
node/geometry classifications use the SDK's named virtual getters, through
checked Microsoft x64 call bridges; no GCC game classes are instantiated.
`BSFixedString` points to a StringCache entry, not directly to characters.
Names follow published external-entry links (at most 32, cycle checked), then
read narrow or validated UTF-16 data at the published entry data field. No
reference count is changed. Invalid surrogates or unterminated/long data are
explicitly unavailable. RTTI names are separate direct character pointers.

Each transform contains `rotation` (3 rows × 3 scalars), `translation` (3),
`scale`, and `rotation_padding` (3). The SDK stores NiMatrix43 as three rows of
four floats. The fourth lane is preserved separately, without interpreting it
as a rotation coefficient. Every scalar is `{ "bits": "XXXXXXXX", "decimal": n }`.
Bits are authoritative, including signed zero and NaN payloads. Finite values
are emitted as exactly representable doubles; nonfinite decimals are JSON null.
No matrix transpose/composition convention is baked into the dump.

For SDK-exposed BSGeometry, `skin_present`, `skin_complete`, and `skin_bones`
record the published skin instance's ordered bone pointers and names. They do
not claim bind matrices, clip identity, time, blend weights or animation curves.

Skin slots with null/unreadable bone pointers retain their slot, pointer and
null name. `skin_missing_names` counts them; one diagnostic per affected mesh
avoids exhausting the diagnostic budget on repeated null slots. Root scopes
`transforms_and_hierarchy_complete`, `names_complete`, `types_complete` and
`skins_complete` keep independently available observations distinct. Overall
`complete` still requires every scope. A missing skin name does not erase or
stop otherwise readable node transforms.

Defaults per root: 4096 visited nodes, depth 128, 8192 child slots, 2048 bones
per skin, 32768 total bone-name entries and 128 stop diagnostics. Reaching the
diagnostic bound halts traversal explicitly. A visited-address set detects cycles/shared objects. Sparse null child
slots are skipped, as the SDK instructs. Invalid arrays, null roots, unreadable
pointers/names, getter faults, parent mismatches and bounds produce `stops` with
parent index, child slot, depth, address and reason. Truncation never claims
complete. Reads use ReadProcessMemory; foreign calls use SEH-protected bridges.
Capture runs synchronously on the engine console callback thread, not DllMain.

## Offline reader and validation

`python3 tools/node_dump.py FILE.json` validates the schema, dimensions, exact
float/decimal agreement, DFS parent order, uniqueness and completeness before
printing a summary. `load`, `tree`, `transform`, and `scalar` are reusable Python
functions. Nonfinite values remain observable and must be excluded explicitly
from numerical comparisons. Incomplete trees remain readable evidence.

`make test` exercises synthetic memory: sparse children, skin bones, cycles,
invalid pointers, overflow, depth/node/array bounds, exact bits and reader
rejection cases. It also compiles the unchanged legacy serializer against
synthetic inputs and compares twelve byte streams to golden v5 digests, while
pinning the legacy readers and writer. This proves fixture/source compatibility,
not equality of captures from different live timestamps or poses.
