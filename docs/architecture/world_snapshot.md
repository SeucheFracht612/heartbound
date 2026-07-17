# World Snapshot Bridge

`WorldSnapshotBridge` connects the runtime world model to the typed save snapshot model.

Implemented foundation:

- export from `WorldState` to `SaveSnapshot`
  - the runtime voxel type-to-prototype manifest is persisted
  - chunk edit logs become chunk delta save records
  - build objects, cargo, inventories, workpieces, assemblies, processes, fires, and mod state
    are copied into their separate save sections
  - persistent entities are exported with stable save ids and transforms, not runtime handles
  - physical-resource records use the entity section with a typed encoded-state payload
  - workpiece grids are encoded through the workpiece grid text codec
  - already-preserved missing-prototype placeholders round-trip as their own section

- import from `SaveSnapshot` to `WorldState`
  - save metadata is validated before runtime state is created
  - `import_validated_snapshot` runs full save snapshot validation against the active prototype
    registry before runtime records are materialized; unavailable object prototypes are first
    preserved as inspectable placeholders with their original record blob
  - raw chunk import validates edit chains and applies their canonical final cells; it does not run
    terrain generation or prove the first saved cell matches a generated baseline
  - workpiece grids are decoded back into local microvoxel grids
  - cargo records must retain finite position, valid mass, volume, stability, known transport
    mode bits, and hazard tags
  - persistent entities receive fresh runtime handles and session net ids
  - encoded physical resources are reconstructed as resource records; physics-body recreation
    remains runtime-owned rather than part of serialization
  - restored entities must have valid saved identity, prototype ids, known entity kinds, and
    finite non-zero-scale transforms
  - the save id allocator resumes past build, entity, cargo, workpiece, assembly, fire, and
    non-process placeholder ids; the process allocator independently resumes past process and
    missing-process ids
  - allocator exhaustion at `u64` maximum is rejected
  - inventory and process owners must reference saved world objects
  - inventory stacks must have valid prototype ids and counts within their saved max count
  - process instances must have known state, valid work/time counters, and valid input/output slots
  - assembly roots and parts must reference saved build pieces
  - assembly part names and port names must be unique within each record
  - duplicate chunk edit records and duplicate mod-state keys are rejected
  - invalid surviving prototype references are rejected by the validated import path

`import_snapshot` is the lower-level storage/migration path. It validates metadata and structural
store invariants, but it has no active prototype registry and therefore cannot establish prototype
existence or expected kinds. Runtime/load-facing code should use the validated path. Likewise, a
streaming load should generate each chunk and call `insert_generated_with_saved_edits`; the raw
bridge's empty-chunk reconstruction exists for low-level snapshot tools and fixtures.

This bridge keeps `SaveSnapshot` as a serialization contract and `WorldState` as the
runtime ownership model. Network graphs are still treated as rebuildable derived data;
they are owned by `WorldState` but not yet serialized as authoritative save state.

`heartstead_world_inspector` uses this bridge to import text snapshots and then prints
the resulting live `WorldState` inspection, including allocator identity and runtime
database counts.
