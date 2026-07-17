# Replication Architecture

Replication is the engine-owned bridge from committed authoritative mutations to client
observation.

Implemented foundation:

- `ReplicationBatch`
  - server-owned monotonic replication stream sequence
  - original source command sequence and source client id
  - source command type
  - committed world-operation events
  - reserved stable save ids

- `ReplicationTextCodec`
  - deterministic text payload for tests and early tools
  - validates payload magic, stream/source identity, command type, events, and reserved ids

- `ReplicationRelevancePolicy`
  - defaults to broadcast behavior for clients without explicit interest rules
  - can restrict a client to events whose subject `SaveId` is visible to that client
  - treats invalid subject ids as global events gated by `receives_global_events`
  - produces a deterministic `ReplicationRelevanceReport` with relevant and filtered client counts
  - filters the actual per-recipient events and reserved ids instead of only deciding whether to
    send an unfiltered batch

- `ReplicationIntake`
  - summarizes decoded client-side replication batches without mutating client world state
  - reports queued batch, event, and reserved-id counts
  - records first/last accepted command sequence and whether queued batch sequences are strictly
    increasing
  - distinguishes global events from saved-subject events so world apply and future reconciliation
    layers can route them explicitly
  - produces an inspectable `ReplicationIntakeReport` for client debug panels and tests

- `plan_replication_delta`
  - world-layer planner that reads a committed `ReplicationBatch` against authoritative
    `WorldState`
  - aggregates duplicate saved-subject events by `SaveId`
  - classifies each subject across the separate world stores: build piece, persistent entity,
    cargo, assembly, owner inventory, workpiece, and owner processes
  - preserves events without a valid saved-subject id separately instead of forcing them into
    saved-object replication
  - marks unresolved subject ids as requiring snapshot/resync fallback
  - produces an inspectable `WorldReplicationDeltaPlan` consumed by the current typed text-delta
    codec and transport bridge

- `materialize_replication_delta`
  - turns the delta plan into typed record sections without inventing a universal replicated
    object model
  - reuses existing save/runtime section types for build pieces, persistent entities, cargo,
    inventories, workpieces, assemblies, and processes
  - removes unrevealed server-only workpiece flaw bits from the public materialized record
  - preserves global events in the embedded plan and keeps unresolved subjects visible as
    resync-required diagnostics
  - produces an inspectable `WorldReplicationDeltaSnapshot` for the current text payload codec and
    future binary codecs

- `WorldReplicationDeltaSnapshotTextCodec`
  - deterministic early text payload for typed replication delta snapshots
  - stores the delta plan header separately from the typed section payload
  - embeds `SaveTextCodec` snapshot text for materialized build/entity/cargo/inventory/workpiece/
    assembly/process sections
  - rejects unsupported sections, missing embedded snapshots, and aggregate count mismatches

- `filter_replication_delta_snapshot`
  - applies the same recipient visibility policy to the plan, events, and every typed record
    section
  - revalidates the filtered aggregate counts before the snapshot is sent

- Typed delta transport bridge
  - world-owned replication payload type: `replication.world_delta_snapshot`
  - wraps typed delta snapshots in reliable replication `TransportMessage` envelopes
  - host sessions expose a payload-agnostic replication-message send hook for world-owned typed
    payloads
  - decodes typed delta envelopes only in the world layer
  - validates message kind, reliable channel, payload type, embedded snapshot, and sequence match
  - keeps the net layer responsible for bytes/session delivery, not world-store semantics

- `apply_replication_delta`
  - world-layer apply helper for typed replication delta snapshots
  - rejects partial deltas that require snapshot/resync fallback before mutating client world state
  - upserts build pieces, persistent entities, cargo, inventories, workpieces, assemblies, and
    processes through their separate world stores
  - preserves client-local runtime handles and session net ids for existing persistent entities
  - marks build-piece and assembly-derived room/network regions dirty for rebuild
  - returns an inspectable `WorldReplicationDeltaApplyReport` with inserted and updated counts

- `apply_client_replication_deltas`
  - world-layer adapter from `ClientSession` replication intake to typed delta application
  - drains queued event batches from the client protocol session
  - matches decoded typed delta snapshots by authoritative command sequence
  - applies matched deltas through `apply_replication_delta`
  - reports subject-event batches that still need typed deltas instead of guessing from local state
  - rejects duplicate decoded delta sequences before draining the client queue
  - keeps `ClientSession` free of world-store knowledge

- `drain_client_replication_delta_snapshots`
  - world-layer adapter for queued non-event replication envelopes in `ClientSession`
  - drains only `replication.world_delta_snapshot` envelopes, leaving unrelated future replication
    payloads queued for their owning layer
  - decodes snapshots through the world-owned transport bridge
  - keeps typed delta decoding out of the net/client protocol layer

- `apply_client_queued_replication_deltas`
  - drains queued typed delta envelopes from `ClientSession`
  - reuses `apply_client_replication_deltas` to match decoded snapshots against queued event
    batches
  - supports event batch and typed snapshot envelopes sharing the same authoritative command
    sequence without treating the typed snapshot as a replayed event batch

- `materialize_replication_deltas_for_tick`
  - world-layer bridge from `HostSessionTickResult` to typed replication deltas
  - materializes only successful commands that committed world mutations and emitted events
  - reports failed, read-only, and eventless commands as skipped with explicit reasons
  - preserves skipped command error codes, messages, and rollback trace summaries for debug
    inspection without serializing those traces to clients
  - keeps the net layer free of build/entity/cargo/inventory/workpiece/assembly/process store
    knowledge

- `send_replication_delta_snapshots_for_tick`
  - world-layer delivery bridge from materialized tick deltas to host-session client queues
  - matches materialized command deltas to host tick relevance reports by authoritative command
    sequence
  - sends complete per-client-filtered `replication.world_delta_snapshot` messages only for
    records visible to that recipient
  - skips partial deltas that already require snapshot/resync fallback instead of delivering
    incomplete authoritative records
  - returns an inspectable `WorldReplicationDeltaDeliveryReport` with sent, skipped, unmatched
    relevance, and resync-skipped counts
  - keeps `HostSession` payload-agnostic: networking moves replication messages, world code owns
    typed delta meaning

- `derive_replication_relevance_policy`
  - world-layer adapter that derives per-client interest rules from `WorldState`
  - uses simulation subjects, viewers, and the generic simulation LOD policy to decide which saved
    subjects are visible to each client
  - keeps non-saved derived subjects such as networks and chunk regions out of saved-subject
    relevance rules
  - can produce an inspectable `WorldReplicationInterestReport` with subject counts, per-viewer
    visible subject counts, LOD exclusions, and skipped non-saved subject counts
  - can refresh a live `HostSession` with the derived policy while returning the same report for
    inspection
  - rejects invalid or duplicate viewer net ids before creating rules

- Host-session relevance filtering
  - after a mutating command commits, `HostSession` sends a command result to the command
    sender
  - it evaluates connected clients through the configured replication relevance policy
  - the current policy can be refreshed by the world-layer helper before a tick, allowing
    world-derived viewer movement or LOD changes to affect the next committed batch without
    teaching `HostSession` about world stores
  - it sends `replication.world_events` only to clients whose relevance decision accepts the batch
  - tick results retain the relevance report and are inspectable as one authoritative network tick
  - failed commands and read-only commands do not produce replication batches

- Client-side intake
  - `ClientSession` validates server replication envelopes before queuing decoded batches
  - duplicate or older replication sequences are rejected before gameplay/UI code can consume them
  - non-`replication.world_events` payloads are queued as raw envelopes instead of being decoded by
    networking code
  - queued batches can be summarized through `ReplicationIntakeReport`
  - world code can drain queued typed delta envelopes and queued event batches through
    `apply_client_queued_replication_deltas`

This is not full state synchronization yet. It establishes authoritative event replication, a
first engine-level interest filter derived from world simulation subjects, and typed world-store
materialization and apply for early state deltas. The deterministic text delta codec is the current
reliable transport payload as well as a tests/tools format. Later systems still need production
binary delta transport, prediction reconciliation, and bandwidth policy.
