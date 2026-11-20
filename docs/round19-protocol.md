# Prospective schema 3 validation

Developed with AI assistance from GPT-6.1 Sol (OpenAI). Original code remains
MIT OR Apache-2.0; external SDK and reference licenses remain separate.

Round19 explicitly replaces the earlier hypotheses for new schema-3 captures.
The old schemas, hypotheses and failed observations remain unchanged.

Before any launch freeze the reader commit, extracted skeleton scopes, JSON
forecast and binary forecast SHA256. The binary `bones19.bin` contains magic
`OSPB19V1`, two little-endian u32 counts (third then first person), and each
view's u16-length ASCII root name followed by parent-i32/name-u16 records.
Parent -1 means the external Root. Other parents must precede their children.
Names are bounded, printable ASCII and unique under ASCII case folding. The
plugin checks bounds and trailing bytes; its FNV identity is not a security hash.
The host records SHA256 independently.

While paused, the host atomically writes the usual Documents `control.tag`:

```text
<session tag>
nodes3=<eight hexadecimal FormID digits>
pause=<eight hexadecimal nonzero pause digits>
```

Then it invokes the existing SDK-slot alias `ospnodes`. There is no new engine
argument parser. Schema3 has an independent counter and filename. Its inherited
fields retain schema2 meanings; additional fields do not change legacy outputs.

The four predictions frozen in the ignored forecast are:

1. Array names and order equal strict Root descendants in reader DFS order,
   ignoring ASCII case. Each differing spelling constructs an engine fixed
   string using the file spelling. Its Entry pointer must equal the bone's
   name Entry. Every constructed key is released before JSON allocation.
2. Every parent index equals the file index, with external Root represented by -1.
3. Every nonnull node points into the ordinary tree, shares the name Entry,
   and has all sixteen world words equal to the record. Local transforms are
   reported side by side without a local-equality requirement.
4. Compose using node local/world where node exists, record local/world
   otherwise, including the parent. The external boundary is the flattened
   root's ordinary world transform. Use the R17 row convention and sequential
   float32 multiplication/addition. Test both preservation of subnormals and
   sign-preserving flushing of subnormal results. Do not change MXCSR or flush
   input operands. All thirteen semantic words must match exactly.

Software rounding decodes float words into normal double values and rounds the
result using integer nearest-even logic, independent of the game's FTZ/DAZ
state. Unit tests compare normal/subnormal boundary cases and random operations
with native float32. An independent Python implementation rechecks every row.
This tests the frozen arithmetic hypothesis, not the game's implementation.

`candidate_round19` becomes `validated_round19` only after both player trees
pass all four proofs in two distinct increasing host pause IDs in the same
plugin session and forecast. A failed applicable proof clears that gate. The
host must independently prove the two pauses from the protocol, not invent
new IDs in a single pause. Its analyzer independently rechecks the proofs and
requires both prospective captures before reporting pose metrics. Readability
and layout validation are separate scopes.

New fields preserve each name Entry, string-key lifetime/probe result, node
local/world, effective source/local/world and strict/FTZ predictions. No local
meaning is inferred from equality. The reader reports per-bone file distance
and near-SO(3) quaternion angle only after validation; malformed rotations have
an unavailable angle. Record locals are compared separately with file locals,
effective locals and other pauses. Preserve complete raw words as evidence.

The four-proof scope is independent of schema2's legacy `complete` flag. In
particular, a raw deferred-attach byte outside 0/1 remains a legacy diagnostic;
it is preserved unchanged and is not interpreted by the four pose/layout
proofs. Schema3 rereads bounded count, expanded count, array pointer, every
record, names and effective finite transforms for its own proof scope. It
never repairs names, parents or composition after a failed proof. The enclosing
legacy completeness flag may remain false while all four specific proofs pass.
An incomplete proof may omit additional effective fields; the reader must
preserve that result instead of crashing or supplying replacement values.
