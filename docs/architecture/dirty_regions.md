# Dirty Region Rebuilds

Derived engine data should be rebuilt by spatial invalidation, not by full-world scans.

Implemented foundation:

- `DirtyRegionTracker`
  - records spatial regions that need derived-system rebuild work
  - merges overlapping or touching regions of the same kind
  - supports consuming regions by kind or all at once
  - exposes structured inspection fields for total regions, sequence span, first region, and
    per-kind rebuild counts

- `DirtyRegionKind`
  - chunk mesh, collision, and lighting rebuilds
  - room graph rebuilds
  - road, cart access, storage access, power, ward, smoke ventilation, water, and logistics
    network rebuilds

- Chunk database integration
  - voxel edits can mark chunk mesh/collision/lighting rebuild regions
  - boundary edits include existing neighbor chunks without mutating neighbor voxel data

- Room and network integration
  - `RoomGraph::mark_dirty_region`
  - `SpatialNetwork::mark_dirty_region`
  - authoritative build-piece placement marks room regions and any network regions exposed by
    placed piece ports
  - build-piece completion and assembly creation also rebuild derived spatial networks in the
    same transaction, leaving dirty regions as scheduling/debug metadata for localized work

Dirty regions are scheduling metadata. They are not authoritative save data. Chunks,
build pieces, networks, rooms, and other systems remain separate representations; dirty
regions only describe which derived outputs need to be rebuilt after mutations.

`WorldState` inspection includes per-kind dirty-region counts so tools can show whether
chunks, rooms, or specific network graphs need rebuilding without inspecting every system
privately.
