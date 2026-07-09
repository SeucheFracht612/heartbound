# Save Text Codec

The text codec provides versioned text persistence for save metadata and typed save
snapshots. It remains useful for tests, golden files, and inspector-friendly fixtures
alongside the binary snapshot codec.

Implemented foundation:

- magic header
- schema version
- game version
- world seed
- enabled mod records
- prototype hash strings
- migration history
- typed snapshot records for chunk edits, build pieces, entities, inventories, cargo,
  workpieces, assemblies, processes, and mod state
- percent escaping for delimiter-safe text
- validation after decode

Text snapshot assembly ports are written as name, network kind, source build-piece save id,
and capacity. The decoder still accepts the older name/kind-only field shape for fixture
readability, but current validation requires saved assembly records to carry a valid source
build-piece id before import.

Text entity records are written with a transform field. The decoder still accepts the older
five-field entity shape and materializes an identity transform so early fixtures remain readable.

Text cargo records are written with a position field. The decoder still accepts the older
seven-field cargo shape and materializes cargo at the origin for compatibility.

Migration history is written by the save migration runner after each ordered migration
step succeeds. Future save work should keep derived data rebuildable and permanent
identity stable. World objects, chunks, workpieces, assemblies, processes, inventories,
cargo, and mod state should stay as separate sections or tables rather than one
universal object blob.
