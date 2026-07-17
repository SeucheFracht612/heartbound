# Save Snapshot Sections

The save snapshot model keeps major world representations in separate typed sections.

Implemented foundation:

- metadata
- voxel palette manifest
- chunk edit records
- build piece records
- entity save records
- inventory records
- cargo records
- workpiece records
- assembly records
- process records
- fire records
- mod state records
- missing-prototype placeholder records

The validator checks:

- save metadata validity
- voxel palette type/prototype uniqueness and reserved-air rules
- stable save ids
- duplicate permanent ids across saved world objects
- duplicate chunk edit coordinates
- chunk edit delta decoding and coordinate matching
- build piece record invariants, including valid socket/port/tag ids and construction state
- entity save record invariants, including stable ids, valid prototypes, known entity kinds, and
  finite non-zero-scale transforms
- duplicate inventory owners
- inventory stack prototype references and count bounds
- cargo finite position, mass, volume, stability, known transport mode bitmask, and hazard tag
  invariants
- duplicate workpiece ids
- duplicate process ids
- duplicate fire ids and fire state/time invariants
- duplicate mod state keys
- missing-prototype placeholder shape and per-kind/stable-id uniqueness
- prototype references through `PrototypeRegistry`
- expected prototype kinds for each section
- non-empty opaque payloads where needed
- workpiece grid payload decoding and shape matching
- inventory and process owners
- process instance state, work/time, and interruption invariants
- process slot prototype and count validity
- assembly roots and parts against saved build pieces
- assembly part and port record uniqueness

This is the engine-side contract that binary or database-backed save files should
preserve: typed sections, stable ids, prototype references, and rebuildable derived data.

`SaveTextCodec` can encode and decode the current `SaveSnapshot` as deterministic text.
This is intended for early persistence tests, golden files, and inspector tools.

`WorldSnapshotBridge` exports authoritative `WorldState` data into these typed sections
and imports snapshots back into world state. Runtime-only identities such as entity
runtime handles and session net ids are regenerated on import. Saved chunk edit deltas
are applied through a load-specific chunk path: the voxel cells and exportable edit log are
restored, but chunks are not marked save-dirty or replication-dirty merely because the save was
loaded. The raw bridge path creates an empty chunk when no resident baseline exists; streamed world
loading must instead generate the deterministic baseline and use
`insert_generated_with_saved_edits` so the first saved `previous` cell is checked against it.

`SaveBinaryCodec` can encode and decode the current `SaveSnapshot` as a versioned binary
payload with explicit typed sections. It is the first binary backend foundation; a later
database save system should preserve the same section boundaries.

Text and binary codecs preserve persisted cargo transport mode bitmasks exactly. Unknown
transport bits are rejected by snapshot validation instead of being silently dropped.

`FileSaveDatabase` stores full binary snapshots and independent chunk-delta payloads in a
directory-backed, generation-staged layout. The external chunk table is authoritative whenever its
index exists, including when that index is intentionally empty.

Save snapshots can be inspected through the debug inspection layer to report section
counts and validation issues.

`heartstead_save_inspector` loads snapshot text files, validates prototype references
against the current mod registry, compares saved mod metadata with active prototype
fingerprints, and prints the same structured inspection data used by debug tooling. Its
`--slots <save_slots_root>` mode lists file-backed save slots and renders both aggregate catalog
inspection and per-slot metadata, layout, generation, and chunk-delta inspection fields.

`heartstead_world_inspector` loads the same text snapshot format, imports it through
`WorldSnapshotBridge`, and prints live `WorldState` inspection data. It validates active mods and
saved prototype references before materializing runtime records. Use it when debugging runtime
database counts, dirty regions, and allocator identity after snapshot import.
