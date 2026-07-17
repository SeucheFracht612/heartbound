# Build Pieces and Assemblies

Build pieces are placed settlement construction. Assemblies are logical multiblock
structures made from build pieces.

Implemented foundation:

- `BuildPieceRecord`
  - stable save id
  - prototype id
  - transform
  - sockets
  - network ports
  - material tags
  - room contribution tags
  - construction state
  - validation for finite transforms, known construction states, valid local-id socket/port/tag
    names, unique sockets and ports, non-zero port capacity, and unique material/room tags
  - room extraction source data through contribution tags and network ports

- `AssemblyDefinition`
  - prototype id
  - named required and optional parts with prototype, relative coordinate, construction stage, and
    optional role
  - required ports with relative coordinates
  - ordered construction stages, capabilities, allowed processes, validation rules, room
    requirements, UI panel, capacity, and heat/power requirements
  - materialized from assembly prototypes through `assembly_definition_from_prototype`

- `AssemblyRecord`
  - stable assembly id
  - root build piece id
  - named build piece parts
  - assembly ports bound to the source build-piece save id that supplies each port
  - per-port capacity copied from the supplying build piece
  - root chunk coordinate plus prototype-relative part/port coordinates
  - blueprint/constructing/drying/maiden-firing/ready/operating/failed state, current construction
    stage, monotonic revision, capabilities, attached process slots, failure reason, and custom state
  - unique part names and unique port names within the record
  - validation for valid port source ids and non-zero port capacity
  - derived as a separate simulation LOD subject from its root build-piece transform without
    being stored as a build piece or entity

- `AssemblyValidator`
  - validates required parts by name and prototype
  - validates required ports
  - reports missing, duplicate, and mismatched parts

- `build.complete_piece`
  - server-authoritative command for moving a placed build piece from construction state
    into `complete`
  - uses stable build-piece save ids instead of terrain block coordinates
  - marks room and network derived data dirty because completed pieces can affect rooms,
    access, ventilation, storage, power, wards, and other port-backed systems
  - rebuilds derived spatial networks so completed ports become operational in the same
    transaction

- `assembly.create`
  - server-authoritative direct-completion command for creating a validated ready assembly from
    already completed and correctly positioned parts
  - consumes already placed build pieces by stable save id
  - derives assembly ports from the participating build-piece ports, preserving the source
    build-piece save id and capacity
  - marks assembly and network derived data dirty
  - rebuilds derived spatial networks so assembly access points become operational in the
    same transaction

- staged assembly construction
  - `assembly.start_blueprint` creates a blueprint from a completed root piece and the
    prototype-defined ghost layout
  - `assembly.place_part` accepts a completed build piece only for its named current-stage slot and
    checks its world coordinate against the root-relative layout
  - `assembly.advance_stage` requires every non-optional part in the current stage and materializes
    the final ready record after the last stage
  - every successful lifecycle mutation advances the record revision and marks save/replication
    state dirty

- assembly state machine
  - `assembly.transition` permits ready/operating, ready/drying/maiden-firing/ready, or transition
    into failed state; failure requires a reason
  - `AssemblyRuntime` also provides checked process attach/detach operations with unique process ids
  - `operating` remains a compatibility field and must exactly agree with `state == operating`

- `SpatialNetworkDeriver`
  - materializes complete build-piece ports and assembly ports into rebuildable spatial
    network nodes and ports
  - connects co-located ports of the same network kind so assembly access points can
    participate in storage, smoke, power, ward, water, and logistics graphs

Assemblies should prevent large machines from becoming fragile "scan nearby blocks every
tick" behavior. The game runtime can decide how to build definitions from mod data; the
engine provides the stable validation shape. Aggregate content validation materializes
assembly definitions early so malformed multiblock rules are reported before runtime.

Room extraction consumes build pieces through `RoomExtractionGridBuilder`. Tags such as
`wall`, `enclosure`, `floor`, `foundation`, and `roof` affect derived room blockers and
roof coverage. Network ports can contribute storage, cart, power, ward, or ventilation
access. Build pieces remain separate records; they are not converted into terrain
voxels.
