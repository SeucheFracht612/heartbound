# Save Binary Codec

`SaveBinaryCodec` is the first binary backend for full save snapshots.

Implemented foundation:

- fixed binary magic and format version
- little-endian primitive encoding
- length-prefixed strings and vectors
- explicit typed sections for:
  - metadata
  - chunk edit records
  - build pieces
  - entities
  - inventories
  - cargo
  - workpieces
  - assemblies
  - processes
  - mod state
- decode errors for invalid magic, unsupported format version, truncation, invalid enum
  values, invalid prototype ids, and trailing data

Binary format version 2 stores assembly-port source build-piece ids and per-port capacity, so saved
multiblock machine ports remain tied to stable placed construction records instead of only a loose
port name. Binary format version 3 adds persisted entity transforms; the decoder still accepts
version 2 snapshots and materializes identity transforms for entities. Binary format version 4 adds
persisted cargo positions; older supported binary versions materialize cargo at the origin.
Binary format version 5 widens chunk edit record coordinates from signed 32-bit components to
signed 64-bit components; older supported binary versions are still decoded through the legacy
32-bit chunk-coordinate path. Binary format version 6 adds the authoritative unsigned 64-bit
`world_time`; older supported versions materialize it as zero for migration.

The binary codec preserves the same `SaveSnapshot` contract as the text codec. It does
not collapse saved data into a generic object blob, and it does not save derived data as
authoritative state. Future database-backed saves should keep these section boundaries
as tables or record families.

`FileSaveDatabase` uses this codec for `snapshot.hssb` and stores chunk deltas separately
under a chunk table.
