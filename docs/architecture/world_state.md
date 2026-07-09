# World State Architecture

World state is the engine-owned runtime container for authoritative world data.

Implemented foundation:

- `WorldState`
  - save metadata
  - one metadata-owned authoritative unsigned 64-bit `world_time` with checked advancement
  - save id allocator
  - runtime handle allocator
  - entity net id allocator
  - process id allocator
  - dirty-region tracker
  - region graph
  - chunk database
  - separate stores for build objects, entities, cargo, inventories, workpieces,
    assemblies, processes, derived rooms, spatial networks, and mod state
  - shared saved-object lookup for owner validation; build objects, cargo, persistent entities,
    and assemblies create saved owners, while inventories and processes only reference them
  - deterministic simulation-subject derivation for entities, build pieces, and process owners
    that need LOD planning without collapsing world stores into one object model
  - deterministic runtime-only simulation-subject derivation for derived network nodes and
    terrain chunk regions that do not own permanent `SaveId` values

- `BuildObjectDatabase`
  - keyed by stable `SaveId`
  - validates `BuildPieceRecord` before insertion, including construction state, transform,
    socket, port, material-tag, and room-contribution-tag invariants

- `EntityDatabase`
  - keyed by `RuntimeHandle`
  - lookup by runtime handle, `NetId`, and persistent `SaveId`
  - keeps transient and persistent identity rules separate
  - validates runtime handle, session net id, prototype id, known entity kind, finite transform,
    non-zero scale, and persistent save-id rules before insertion

- `ProcessDatabase`
  - keyed by `ProcessId`
  - validates process id, owner id, prototype id, state, work/time counters, interruption reason,
    and slots before insertion
  - advances timestamp-based processes in batches with either a shared modifier bundle or a
    per-process modifier resolver
  - stages whole-batch advancement before commit, while preserving stable record-node addresses

- `InventoryDatabase`
  - keyed by owner `SaveId`
  - validates stack prototype ids, non-empty counts, non-zero max counts, and max-count bounds

- `NetworkDatabase`
  - owns one spatial graph per network kind on demand
  - can rebuild derived spatial networks from complete build-piece ports and validated
    assembly ports
  - exposes read-only derived network records for simulation LOD and inspection without making
    networks authoritative saved objects

- `RoomGraph`
  - owned by `WorldState` as rebuildable derived runtime state
  - queried by process environment resolution and inspection tooling

- `WorkpieceDatabase`
  - keyed by `WorkpieceId`
  - keeps local microvoxel grids out of terrain chunks

- `WorldSnapshotBridge`
  - exports world state into typed save snapshot sections
  - imports typed snapshots back into world state
  - allocates fresh runtime handles and session net ids for restored entities
  - advances the entity net id allocator after restored entities
  - derives the next process id from saved process records

- `heartstead_world_inspector`
  - prints structured inspection data for an empty runtime world or a text snapshot
    imported through `WorldSnapshotBridge`
  - validates active mods and saved prototype references before materializing snapshot records
    into runtime `WorldState`
  - reports save metadata, allocator identity, per-kind dirty regions, and store counts

The important boundary is ownership, not feature richness yet. Regions, terrain chunks,
placed build pieces, entities, workpieces, cargo, derived rooms, networks, assemblies, and
processes now have a shared runtime home without being stored as one universal
block abstraction.

Save snapshots remain serialization records. They can now be produced from world
state and loaded into world state, but they are not the primary in-memory world
model.
