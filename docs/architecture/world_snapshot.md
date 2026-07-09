# World Snapshot Bridge

`WorldSnapshotBridge` connects the runtime world model to the typed save snapshot model.

Implemented foundation:

- export from `WorldState` to `SaveSnapshot`
  - chunk edit logs become chunk delta save records
  - build objects, cargo, inventories, workpieces, assemblies, processes, and mod state
    are copied into their separate save sections
  - persistent entities are exported with stable save ids and transforms, not runtime handles
  - workpiece grids are encoded through the workpiece grid text codec

- import from `SaveSnapshot` to `WorldState`
  - save metadata is validated before runtime state is created
  - `import_validated_snapshot` runs full save snapshot validation against the active prototype
    registry before runtime records are materialized
  - chunk deltas are applied back into terrain chunks
  - workpiece grids are decoded back into local microvoxel grids
  - cargo records must retain finite position, valid mass, volume, stability, known transport
    mode bits, and hazard tags
  - persistent entities receive fresh runtime handles and session net ids
  - restored entities must have valid saved identity, prototype ids, known entity kinds, and
    finite non-zero-scale transforms
  - the save id allocator resumes past the highest stable id in the snapshot
  - duplicate stable save ids across saved world objects are rejected
  - inventory and process owners must reference saved world objects
  - inventory stacks must have valid prototype ids and counts within their saved max count
  - process instances must have known state, valid work/time counters, and valid input/output slots
  - assembly roots and parts must reference saved build pieces
  - assembly part names and port names must be unique within each record
  - duplicate chunk edit records and duplicate mod-state keys are rejected
  - missing or wrong-kind prototype references are rejected by the validated import path

This bridge keeps `SaveSnapshot` as a serialization contract and `WorldState` as the
runtime ownership model. Network graphs are still treated as rebuildable derived data;
they are owned by `WorldState` but not yet serialized as authoritative save state.

`heartstead_world_inspector` uses this bridge to import text snapshots and then prints
the resulting live `WorldState` inspection, including allocator identity and runtime
database counts.
