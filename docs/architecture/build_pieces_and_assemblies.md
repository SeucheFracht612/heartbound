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
  - named required and optional parts
  - required ports
  - materialized from assembly prototypes through `assembly_definition_from_prototype`

- `AssemblyRecord`
  - stable assembly id
  - root build piece id
  - named build piece parts
  - assembly ports bound to the source build-piece save id that supplies each port
  - per-port capacity copied from the supplying build piece
  - operating state
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
  - server-authoritative command for creating validated assembly records
  - consumes already placed build pieces by stable save id
  - derives assembly ports from the participating build-piece ports, preserving the source
    build-piece save id and capacity
  - marks assembly and network derived data dirty
  - rebuilds derived spatial networks so assembly access points become operational in the
    same transaction

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
