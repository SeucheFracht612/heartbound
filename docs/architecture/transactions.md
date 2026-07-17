# Transactional World Operations

Meaningful world changes should follow a shared transaction shape:

```text
begin operation
validate
reserve ids
mutate world
update derived systems
emit events
replicate
mark save dirty
commit
```

Implemented foundation:

- `WorldOperation`
  - records operation stages
  - reserves stable save ids through `SaveIdAllocator`
  - exposes recorded mutations and derived-system updates as read-only debug data
  - emits operation events
  - requires at least one event plus save and replication dirty marks before commit
  - supports rollback/failure tracking
  - reports stable stage names and dirty flags for tools and replay diagnostics

This is not a complete world database yet. It is the active contract for voxel edits, build
placement/completion, inventory transfers, workpiece edits/finishing, cargo creation, entity spawns,
process starts/advancement, sleep, and assembly lifecycle commands. Future tree felling,
machine-specific actions, and ward activation should use the same path to avoid duplication bugs and
multiplayer desyncs.

`ServerCommandDispatcher` is the current entry point for command-driven mutations. It
creates a `WorldOperation` for each mutating command and requires handlers to satisfy the
transaction contract before commit. A mutating operation without an emitted event is rejected so
silent authoritative changes cannot bypass replication and replay diagnostics.
Successful dispatch results retain a local operation trace with the recorded stages, mutation
descriptions, derived-system updates, and dirty flags. Host sessions and replay reports carry that
trace for diagnostics without adding it to client-facing command-result payloads.
For tooling and replay diagnostics, `ServerCommandDispatcher::dispatch_report` also returns a
structured report for rejected commands. If a handler started a transaction before failing, the
report preserves the rollback stage, partial mutation descriptions, emitted diagnostic events,
reserved ids, and the authoritative error code while the older `dispatch` API continues to expose a
simple success/failure result.

`WorldCommandRegistry` provides reusable command handlers for terrain voxel edits and sleep,
build-piece placement/completion, workpiece edits/finishing, inventory transfers, cargo creation,
entity spawns, process starts/advancement, direct assembly creation, and staged assembly
blueprint/part/stage/state transitions. These handlers mutate `WorldState` directly and use the same
`WorldOperation` contract for events, save dirtiness, and replication dirtiness.

Build completion and assembly creation now perform an immediate derived spatial-network
rebuild after marking dirty regions, so operational ports participate in storage, smoke,
power, ward, water, and logistics graphs within the same authoritative transaction.

`Inspector` can render a `WorldOperation` summary with stage sequence, mutation count,
derived-update count, event count, dirty flags, and failure details. This is the common
visibility path for command dispatch, replay, and future debug overlays.
