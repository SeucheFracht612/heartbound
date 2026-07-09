# Chunk Database

World voxels are stored in streamed chunks. The chunk database is the engine-owned
container for those chunks and their edit records. Chunk cells store compact numeric
types; mod-facing voxel meaning is resolved through the voxel palette.

Implemented foundation:

- `VoxelChunk`
  - fixed-size terrain voxel storage
  - cubic chunk coordinates use signed 64-bit `x/y/z` components for near-unbounded world height
    and distance
  - dirty flags for mesh, collision, lighting, save, and replication

- `VoxelPalette`
  - maps stable voxel prototype ids to compact content voxel type ids
  - reserves type `0` for air
  - keeps prototype meaning out of chunk cell storage

- `ChunkDatabase`
  - create/find chunk records
  - expose read-only chunk snapshots for inspection, tooling, and simulation LOD derivation
  - insert generated chunks without creating edit records
  - read and write voxel cells
  - append explicit voxel edit records
  - auto-create edited chunks
  - erase chunk records when the streaming layer unloads them
  - mark edited chunks dirty for mesh, collision, lighting, save, and replication
  - apply saved chunk edit deltas without re-marking loaded chunks dirty for save or replication
    while preserving those deltas for later snapshot export
  - mark existing face-neighbor chunks dirty for rebuild when boundary voxels change
  - expose dirty/edit statistics
  - optionally emit dirty regions for mesh, collision, and lighting rebuild queues
  - preserves signed 64-bit chunk coordinates in dirty-region rebuild queues without clamping

- `ChunkEditDeltaTextCodec`
  - versioned text format for chunk edit deltas
  - preserves signed 64-bit chunk coordinates in saved edit payloads
  - validates saved chunk coordinates against encoded payload coordinates
  - shared by world snapshot export/import and save snapshot validation

- `ChunkStreamer`
  - loads chunk coordinates into `WorldState` on demand
  - generates baseline terrain through the deterministic terrain generator
  - can overlay one saved per-chunk edit delta through an abstract delta-source interface
  - provides a `FileSaveDatabase` adapter that maps a missing per-chunk delta to "no saved edits"
    without exposing save-directory layout to world streaming callers
  - reports whether a request found an already-loaded chunk, generated a fresh baseline chunk, or
    generated a baseline chunk with saved edits applied
  - applies saved deltas through the load-specific chunk path so streamed loads do not create false
    save or replication dirtiness
  - stages generated data plus saved edits transactionally, leaving no loaded chunk when validation
    or delta application fails
  - validates every saved local coordinate before mutation and restores canonical edit history
    without duplicating records across reloads
  - plans viewer-interest load requests from chunk radii without mutating world state
  - bounds load-radius planning and uses overflow-safe distance/range arithmetic at signed
    coordinate limits
  - reports clean chunks outside the retain radius as evictable while pinning save-dirty or
    replication-dirty chunks so terrain edits are not discarded before persistence/replication
  - executes eviction requests by removing only clean loaded chunks, reporting missing chunks and
    retained dirty chunks separately
  - flushes requested save-dirty chunk edit deltas through an abstract sink, with a
    `FileSaveDatabase` sink adapter for streamed persistence
  - clears only the save-dirty flag after a successful chunk-delta write; replication-dirty chunks
    remain pinned until the replication side has handled them
  - flushes requested replication-dirty chunk edit deltas through an abstract replication sink
  - clears only the replication-dirty flag after a successful replication handoff, so eviction can
    proceed only after both persistence and replication have acknowledged the terrain edits
  - can run one viewer-interest maintenance step that plans chunk interest, optionally flushes
    pinned dirty chunks through save and replication sinks, and then evicts every now-clean
    candidate while reporting chunks that still cannot be unloaded
  - can also run a loaded maintenance step that satisfies viewer-interest load requests through
    deterministic generation plus optional saved edit deltas before the flush/evict pass

- `ChunkMesher`
  - extracts renderer-neutral surface mesh data from terrain voxel chunks
  - hides internal faces between adjacent solid cells
  - preserves voxel type and light metadata per vertex

Neighbor invalidation marks rebuild state only. It must not mutate neighboring voxel
data. Save and replication dirtiness remain attached to chunks whose stored data changed.
Generated chunks and loaded saved edit deltas only mark mesh, collision, and lighting
rebuild state. They do not mark save or replication dirty until a player/world operation
changes stored voxel data after load.

Future work:

- budgeted/asynchronous streaming jobs for large load and eviction waves
- greedy/cross-chunk mesh optimization
- neighbor-halo input and prototype-declared rich-block invalidation radii
- collision generation
- lighting propagation
