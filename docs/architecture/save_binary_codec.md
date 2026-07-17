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

The current writer emits binary format version 13. The decoder accepts versions 2 through 13 and
materializes explicit compatibility defaults for fields that did not exist in an older version:

| Version | Change |
| --- | --- |
| 2 | Stores assembly-port source build-piece ids and per-port capacity. |
| 3 | Adds entity transforms; version 2 entities receive identity transforms. |
| 4 | Adds cargo positions; older cargo records are placed at the origin. |
| 5 | Widens chunk-edit coordinates from signed 32-bit to signed 64-bit components. |
| 6 | Adds authoritative unsigned 64-bit `world_time`; older snapshots receive zero. |
| 7 | Replaces legacy global floating-point positions with anchored world positions and adds the voxel-palette manifest. |
| 8 | Adds missing-prototype placeholder records. |
| 9 | Widens process tick fields to unsigned 64-bit values and adds output-claim, condition-function, and interruption-policy state. |
| 10 | Adds fire simulation records. |
| 11 | Adds rich workpiece material, server-state, owner, revision, and committed fields. |
| 12 | Adds assembly state-machine state, stage, revision, capabilities, process slots, failure reason, and custom state. |
| 13 | Adds assembly part/port relative coordinates and the assembly root chunk coordinate. |

Version numbers describe an ordered byte layout, not independently optional features. Any new
authoritative field must increment the format version, retain an intentional legacy default, and
add decode coverage for both the new and oldest-supported layouts.

The binary codec preserves the same `SaveSnapshot` contract as the text codec. It does
not collapse saved data into a generic object blob, and it does not save derived data as
authoritative state. Future database-backed saves should keep these section boundaries
as tables or record families.

`FileSaveDatabase` uses this codec for `snapshot.hssb` and stores chunk deltas separately
under a chunk table.
