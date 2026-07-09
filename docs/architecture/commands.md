# Server-Authoritative Commands

Clients request meaningful world changes with commands. The authoritative server
validates those commands, applies mutations through `WorldOperation`, then emits events
for replication and save dirtiness.

Implemented foundation:

- `CommandEnvelope`
  - sequence id
  - sender net id
  - command type
  - payload string, opaque to transport and replay
  - client timestamp

- `CommandPayload` and `CommandPayloadTextCodec`
  - deterministic key/value payload format for structured engine commands
  - lowercase validated field keys
  - percent-escaped delimiters and line breaks
  - duplicate-key and malformed-escape rejection
  - string field values that handlers parse into command-specific typed data

- `CommandExecutionContext`
  - executor role
  - server timestamp
  - save id allocator
  - prototype registry
  - authoritative `WorldState` pointer for handlers that mutate world data

- `ServerCommandDispatcher`
  - registers command descriptors
  - rejects unknown commands
  - rejects mutating commands outside authoritative-server execution
  - runs handlers with a `WorldOperation`
  - commits mutating operations only when an event is emitted and save/replication dirty states are
    marked
  - returns a local operation trace with stages, mutation descriptions, derived updates, and dirty
    flags for diagnostics and replay reports

- `WorldCommandRegistry`
  - registers engine-owned command handlers
  - `world.set_voxel`
    - decodes a structured command payload with `chunk`, `voxel`, and `cell` fields
    - mutates terrain chunks through `WorldState`
    - marks chunk rebuild dirty regions through `ChunkDatabase`
  - `build.place_piece`
    - decodes a structured command payload with `prototype` and optional transform fields
    - reserves a stable save id
    - validates the build-piece prototype through `PrototypeRegistry`
    - materializes prototype material tags, room contribution tags, and named network ports
    - inserts a `BuildPieceRecord` into the build-object database
    - marks room and port-related network dirty regions for derived-system rebuilds
  - `build.complete_piece`
    - decodes a structured command payload with a stable build-piece object id
    - transitions an existing build piece to `complete`
    - rejects missing or already-complete build pieces
    - marks room and port-related network dirty regions
    - rebuilds derived spatial networks from complete build-piece and assembly ports
  - `workpiece.edit_cell`
    - decodes a structured command payload with `workpiece_id`, `operation`, `coord`, and
      optional `cell` fields
    - finds the stable workpiece record in `WorldState`
    - applies the local grid operation through `WorkpieceGrid`
    - emits a workpiece edit event without touching terrain chunks or reserving build-object ids
  - `inventory.transfer_items`
    - decodes a structured command payload with source owner, destination owner, source slot,
      destination slot, and count fields
    - finds owner-keyed inventory records in `WorldState`
    - transfers an exact stack count through the shared inventory transfer helper
    - emits an inventory transfer event without touching cargo or physical transport state
  - `process.start`
    - decodes a structured command payload with owner and process prototype fields
    - validates that the owner `SaveId` references an existing saved world object
    - validates the process prototype through `PrototypeRegistry`
    - reserves a process id through `WorldState`
    - inserts a timestamped `ProcessInstance`
  - `process.advance_all`
    - accepts an empty command payload; client-supplied process rate modifiers are rejected
    - advances all running processes against authoritative server time
    - materializes process prototype definitions for room, power, and quality requirements
    - resolves per-process room and power modifiers from `WorldState` derived rooms and networks
    - emits a batch process advancement event when at least one process changes state
    - rejects zero-change calls so mutating commands do not commit empty transactions
  - `cargo.create`
    - decodes a structured command payload with a cargo prototype field and optional `position`
      field
    - validates the cargo prototype through `PrototypeRegistry`
    - materializes a `CargoDefinition`
    - reserves a stable cargo save id and inserts a `CargoRecord`
  - `entity.spawn`
    - decodes a structured command payload with an entity prototype field and optional
      `position`, `rotation`, and `scale` fields
    - validates the entity prototype through `PrototypeRegistry`
    - validates the initial transform before reserving runtime, network, or save identity
    - reserves runtime and session net identity through `WorldState`
    - reserves a stable save id only for persistent entity definitions
    - inserts a validated `EntityRecord`
  - `assembly.create`
    - decodes a structured command payload with assembly prototype, root build piece, and named
      build-piece parts
    - materializes and validates an `AssemblyDefinition`
    - derives assembly parts and required ports from placed build pieces
    - reserves a stable assembly save id
    - marks assembly/network derived data dirty and rebuilds derived spatial networks

The command dispatcher is not responsible for sockets or packet delivery. Transport
messages are converted into `CommandEnvelope` values, then the dispatcher validates and
executes them on the authoritative server.

The transport host rejects replayed or out-of-order reliable command sequences before
they reach the dispatcher. Command handlers still remain responsible for semantic validation,
authority checks, transactional mutation, and id reservation.

Committed operation events are converted into `ReplicationBatch` payloads by the host
session so connected clients can observe authoritative world changes.

`CommandReplayRunner` executes recorded command envelopes through this dispatcher so
command behavior and transaction traces can be inspected without a live transport connection.

Transport and replay still carry command payloads as strings, but structured engine
commands now use the deterministic command payload codec inside that string. Production
networking can replace the byte transport later without changing handler ownership or
world-operation semantics.
