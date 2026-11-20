# JSON schema version 4

Files contain one UTF-8 JSON object and a trailing newline. Timestamps use UTC
RFC3339 with milliseconds. `session_id` is 32 hexadecimal characters generated
once per Load; `sequence` starts at 1 for each process. Each filename is
`<session_id>-<sequence>.json`.

| Field | Meaning |
| --- | --- |
| schema_version | Integer 4. Plugin metadata version 5 adds backward-compatible equipped-weapon observations. Settings list coverage semantics from schema 2 are retained. |
| complete | Full mode: settings list coverage and mod/player/native-module collection flags are true. Light mode: player.complete only. Omitted sections are not errors. |
| capture_mode | `full` (default and automatic events) or `light` (explicit console request). |
| game_settings_coverage | `configured_names` in full; `omitted_by_request` in light. Never all engine settings. |
| timestamp_utc, session_id, sequence | Capture identity. |
| event | `post_load_game`, `new_game`, or `console_snapshot`. |
| event_phase | `load_routine_returned`, `new_game_created_before_game_loaded`, or `console_handler`. |
| tag | Optional tag read from control.tag for console captures; empty for automatic events. |
| tick_ms, windows_process_id | GetTickCount64 and Windows process identity. Under Wine this is the Windows PID, not the host's. |
| console_commands | Observed long/short names from both SDK command tables in full captures, including ready; null in light mode. |
| capabilities | ospsnap registration status; light_capture=true; console_tag_argument=false, tag_control_file=true, native_command_runner=false, hit_events=false. |
| game | `version` and packed `runtime_hex`, from the F4SE interface. |
| f4se | `version`, `packed_hex`, `release_index`. |
| plugins | `complete` and `items` for loaded game plugins. Each item has `name`, `kind` (`full`/`light`), numeric `mod_index`, nullable numeric `light_index`, `record_flags_hex`. Bad records instead have null name and `status=unreadable`. |
| native_plugin_modules | `module_enumeration_complete`, `registration_status=not_enumerable_by_public_f4se_api`, and items containing module, path, query_export, load_export. |
| game_settings | Current observations for unique configured names, with coverage/counts/errors described below. |
| player | complete, nullable level, level_source, actor_values. Rows have array_index, nullable editor_id/form_id/base/current/maximum. Maximum uses the published ActorValueOwner slot 2. |
| errors | Array of diagnostic strings also written as single log lines. |
| string_encoding | Describes UTF-8 preservation and invalid-byte escaping. |

`plugins.complete` describes only its own collection. `player.complete` describes
only successful reads at that callback and does not assert completion of new-game
initialization. In full mode root `complete` combines those collection flags with
game_settings.complete; in light mode it reflects player.complete alone. Missing/unsupported values remain visible even when
name-list coverage is complete.

FormIDs and packed values are uppercase hex strings without `0x`. All AV numbers
are float32 observations represented as JSON numbers; negative zero may parse as
ordinary zero. Non-finite or unreadable values are null and logged. A getter can
return an empty EditorID; empty string differs from unreadable/null.

Strings preserve valid UTF-8. Every control character, quote and backslash is
escaped. Malformed UTF-8 bytes become `\u00xx` individually; this preserves the
byte's value as a Unicode codepoint but is not a locale/codepage conversion.
This makes JSON valid even for arbitrary mod-provided strings.

## GameSettings section

| Field | Meaning |
| --- | --- |
| coverage, all_engine_settings | `configured_names`, false. |
| source_file | Resolved path to osp_measure_settings.txt beside the actual DLL, re-read each full capture. |
| requested_count | Number of valid unique names after trimming/comments/deduplication. |
| queried_count | Calls completed according to the SDK wrapper contract, including confirmed misses. |
| found_count | Completed calls returning a non-null Setting pointer, even if a later value read fails. |
| complete | Input has no errors, is nonempty, and queried_count=requested_count. |
| values_complete | Complete coverage, every item found/read, and no item diagnostics. |
| status | `list_queried` or `incomplete`. |
| duplicate_names_ignored | Number of identical repeated names. |
| input_errors | File/parser errors. Invalid lines prevent complete=true even if all accepted names were queried. |
| items_with_errors | Number of rows with at least one diagnostic. |
| items | One row per valid unique name, in first-occurrence order. |

| Item field | Meaning |
| --- | --- |
| name | Requested name. |
| lookup_completed | true only if the engine call returned normally with the SDK output contract. |
| found | true/false for a completed lookup; null if it could not be completed. |
| registered_name | Actual current Setting.name, nullable. Dynamic string names may start with S even if requested with s. |
| type, type_prefix | SDK-derived type and actual registered first character, nullable. |
| value | Current decoded value, explicitly raw u32 for unknown prefixes, or null if unavailable. |
| value_encoding | `decoded`, `raw_u32`, or null. |
| raw_bits_hex | Entire eight-byte payload as 16 hex characters; unused upper bytes of scalars have no semantic guarantee. |
| raw_u32 | Low 32 payload bits as an unsigned JSON number, nullable. |
| errors | Item diagnostics; a confirmed miss is recorded as `not found`. |

SDK types are integer (signed int32), float (float32), bool (low byte), string
(s/S current text), id6 (u32 shifted right eight), id (u32), and unknown. The
SDK classifies u/c/h and unrecognized prefixes as unknown. For those the runtime
exposes Data.u32 with value_encoding=raw_u32 and an item diagnostic. It does not
claim a new semantic engine type. Non-finite floats and unreadable/null string
pointers produce null values and errors.

A confirmed miss can coexist with complete=true and values_complete=false.
A successfully queried unknown-prefix item also keeps coverage complete; its
diagnostic prevents values_complete. A constructor/lookup fault has found=null
and incomplete coverage. A release fault is logged even if lookup completed.
Empty, inaccessible, truncated, oversized or malformed input cannot produce a
complete settings section.

## Comparison report

`tools/compare_settings.py` maps an export of the form
`name -> {type, formid, file_value}` to runtime rows by exact requested name.
Its disjoint equal/different/not_found/not_queried/not_compared counts sum to the
baseline count. Differences contain both values and types. Extra runtime names
are counted but lie outside baseline coverage.

Only completed misses count as not_found. Missing rows, failed calls, and legacy
schema-1 captures with no settings count as not_queried. Known type changes are
differences; unavailable/unsupported values are not_compared. Explicit raw_u32
values for u compare against the file integer with that raw basis disclosed.
Floats compare exactly unless finite nonnegative tolerances are requested.
Strings are not_compared because the baseline contains IDs rather than English
text and no English .STRINGS table is decoded in this version.

The comparison report has report_schema_version=1. The snapshot's completeness
and coverage are reported without overriding category counts. Inputs, runtime
captures, installed name lists, ESM exports and comparison outputs are game data
and remain under ignored build/ during development. Tests use synthetic records.

## Console tag protocol

`ospsnap` is a zero-argument engine console command. A missing or empty
control.tag means an empty tag and full mode. The file is at most 192 bytes:
first line is up to 160 ASCII letters/digits or `_-. :` without the space;
optional second line is **exactly** `mode=light` or `mode=full`. LF/CRLF and one
terminal newline are accepted. Spaces, tabs, extra lines, non-ASCII, embedded NUL,
unknown modes and oversized input are rejected, logged, and emit no capture.
The mode applies only to that console invocation; automatic events remain full.

Example control file (not a console argument):

```text
run-token:burst
mode=light
```

In light mode no settings file lookup, mod/native-module enumeration or command
catalogue collection runs. `game_settings`, `plugins`, and
`native_plugin_modules` each contain precisely the omission marker:

```json
{"status":"omitted_by_request","complete":null,"values_complete":null,"requested":false}
```

There is no items array for an omitted section. Null completeness is neither an
assertion of coverage nor an incomplete-by-error result. Player/ActorValue errors
are still retained and logged normally. Versions, event/phase, session, sequence,
tag, timestamp/tick, PID and player level/AVs remain present. Root complete in
light mode reports only player collection success. The full section contracts
above remain unchanged. Consumers must read capture_mode/status before interpreting
completeness or missing items. Schemas 2/3 lacked an intentional omission mode.

A tool that types into the console can replace control.tag atomically before
each `ospsnap` and restore it afterwards; the plugin only reads it. Several
captures may share one tag: `session_id` and the contiguous `sequence` numbers
tell them apart.

The timestamp/tick identify the synchronous capture, not an exact game instant.
Full snapshots pause console execution while collected. No event row should be
interpreted as a recorded hit: the SDK publishes no hit events.


## Plugin version 5 additive observations

`plugin_version: 5` keeps schema 4 and its existing completeness semantics.
`complete_scope` explicitly excludes the independently qualified equipment
extension. `player.equipped_weapon` is present in full and light captures.
It reports `status: none` only after a valid equipment array contains no WEAP.
Ambiguity, unreadable/null instances, overlarge arrays and cycles are errors,
never substituted base data. `instance_complete` qualifies named instance
fields; `mods.complete` qualifies the object-instance FormID list. The overall
section is incomplete while SDK-unpublished aim/zoom fields are unavailable.
`instance_address_hex` is process-local identity evidence, not a persistent ID.
Absent zoom is recorded explicitly; a present zoom with unknown FOV is not zero.
All numerical instance fields come from memory. `aim.fields` and `zoom.fov`
remain null; a separate reader may resolve the FormIDs from game records.
`fields.flags_hex` preserves the runtime bits. `automatic_sdk_flag` tests the
SDK mask 0x4000; `bolt_action_sdk_flag` tests 0x400000. These names describe
SDK mask tests; their runtime semantic equivalence is not asserted. `base_damage` is
the SDK's uint16 baseDamage field, not damage after perks or a Pip-Boy card.

Unreadable non-null ammo references make `instance_complete` false. A confirmed
null ammo reference is represented as absent. `mods.complete` stays false after
a partial slot/stack/extra traversal, including when one readable mod list was
found before an unreadable node or cycle. Partial IDs remain explicitly qualified.
