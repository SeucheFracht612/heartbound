# Save Text Codec

The text codec provides versioned text persistence for save metadata and typed save
snapshots. It remains useful for tests, golden files, and inspector-friendly fixtures
alongside the binary snapshot codec.

Implemented foundation:

- snapshot magic header: the writer emits `heartstead.save_snapshot_text.v2`; the decoder accepts
  the explicitly supported v1 and v2 headers
- schema version
- game version
- world seed
- authoritative world time
- enabled mod records
- prototype hash strings
- migration history
- persisted voxel palette entries
- typed snapshot records for chunk edits, build pieces, entities, inventories, cargo,
  workpieces, assemblies, processes, fires, mod state, and missing-prototype placeholders
- percent escaping for delimiter-safe text
- validation after decode

Text snapshot assembly parts and ports include their prototype-defined relative coordinates.
Ports also carry name, network kind, source build-piece save id, and capacity. The decoder accepts
older part records without coordinates and older port records with only name/kind or without
coordinates for fixture readability, but current validation still requires every saved port to
carry a valid source build-piece id before import.

Text entity records are written with a transform field. The decoder still accepts the older
five-field entity shape and materializes an identity transform so early fixtures remain readable.

Text cargo records are written with a position field. The decoder still accepts the older
seven-field cargo shape and materializes cargo at the origin for compatibility.

Current workpiece records preserve material id, opaque server-state encoding, owner session,
revision, and committed state. Current assembly records preserve state-machine, capability,
process-slot, failure/custom-state, and root-coordinate data. Current process records preserve
output-claimed, condition-function, and interruption-policy state. Their explicitly recognized
legacy field counts receive documented defaults rather than being parsed as the current shape.

The decoder rejects metadata above 16 MiB, snapshots above 512 MiB, individual lines above 16 MiB,
and decoded record/collection growth above one million elements. Field splitting is bounded before
allocation, so delimiter-heavy malformed records cannot amplify into unbounded temporary vectors.
Singleton metadata fields may appear exactly once, and the end marker must consume the entire
document. These are parser safety limits, not recommended save sizes.

Migration history is written by the save migration runner after each ordered migration
step succeeds. Future save work should keep derived data rebuildable and permanent
identity stable. World objects, chunks, workpieces, assemblies, processes, inventories,
cargo, and mod state should stay as separate sections or tables rather than one
universal object blob.
