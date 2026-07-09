# Simulation LOD Architecture

Long-lived settlements need different simulation detail depending on player proximity
and load state.

Implemented foundation:

- `SimulationSubject`
  - stable save id for persistent objects
  - optional runtime handle for currently materialized entities
  - stable process id for process-owner subjects
  - prototype id
  - subject kind
  - world coordinate
  - last update timestamp
  - sleeping and forced-LOD state

- `SimulationViewer`
  - network id
  - world coordinate

- `SimulationLodPolicy`
  - full simulation radius
  - simplified simulation radius
  - tick intervals for full, simplified, and sleeping subjects

- `SimulationLodPlanner`
  - classifies subjects as `full`, `simplified`, `sleeping`, or `unloaded`
  - reports due ticks for loaded subjects
  - reports offline deltas for unloaded subjects so timestamp-based systems can advance
    when reloaded
  - validates radius, timestamp, and saved-identity invariants

- `derive_simulation_subjects`
  - world-layer adapter that derives deterministic simulation subjects from `WorldState`
  - emits entity subjects from `EntityRecord` transforms and build-piece subjects from
    `BuildPieceRecord` transforms
  - emits assembly subjects from assembly root build-piece transforms and uses assembly operating
    state for sleeping classification
  - emits process-owner subjects for process instances whose owners currently have spatial
    transforms through build pieces, persistent entities, cargo records, or assembly roots
  - keeps the process owner's `SaveId` as the spatial owner while carrying the process instance's
    `ProcessId` so multiple processes on one object remain distinguishable in frame plans
  - emits non-persistent network subjects from spatial network nodes using deterministic runtime
    handles, so derived networks do not claim saved identity
  - emits non-persistent chunk-region subjects from terrain chunk coordinates using deterministic
    runtime handles, so terrain chunks do not become saved objects or entities
  - keeps the generic LOD planner independent from world database ownership
  - supports filtering subject kinds without merging entity, build-piece, assembly, process,
    network, or chunk storage

- `plan_world_simulation_frame`
  - derives world simulation subjects and classifies them through the generic planner in one
    world-layer call
  - keeps game/runtime callers from needing to know how entities, build pieces, assemblies,
    process owners, networks, and chunks map into generic subjects
  - propagates subject-derivation and planner validation errors without mutating `WorldState`

- `derive_replication_relevance_policy`
  - reuses derived simulation subjects and viewer positions to build per-client replication
    interest rules
  - includes full, simplified, and sleeping saved subjects by default while leaving unloaded
    subjects out of normal replication relevance
  - has an inspectable report form for subject totals, per-viewer visible saved subjects, LOD
    exclusions, and non-saved subject skips
  - has a world-layer helper that installs the derived policy on a `HostSession` and returns the
    report for tooling
  - keeps the generic networking relevance policy free of world database knowledge

- debug inspection
  - exposes policy radii and tick intervals
  - exposes raw subject identity, coordinates, timestamps, persistence, sleeping, and forced-LOD
    state before frame planning
  - exposes per-subject LOD decisions, process ids, due-tick state, and offline delta
  - exposes frame-plan counts and reports inconsistent decision/count summaries as errors
  - exposes world-derived replication interest reports before host sessions consume the
    resulting network relevance policy

The planner is intentionally generic. Game runtime systems should decide what a full,
simplified, or reload-time update means for animals, crops, machines, wards, storage,
outposts, and cargo. The engine owns the shared classification and timing contract.
