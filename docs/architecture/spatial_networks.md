# Spatial Network Architecture

Many settlement systems share graph-shaped behavior:

- roads
- cart access
- storage access
- power
- wards
- smoke ventilation
- water and irrigation
- logistics routes
- outpost links

Implemented foundation:

- `SpatialNetwork`
  - typed network kind
  - nodes with position and capacity
  - read-only node snapshots for inspection, tooling, and simulation LOD derivation
  - edges with quality, capacity, and blockers
  - named ports attached to nodes, with optional owner and source build-piece ids
  - total node, edge, and port capacity summaries for inspection/tooling
  - owned/sourced port counts for inspection/tooling
  - owner-scoped port counts and capacity totals for systems such as power and logistics
  - dirty flag for rebuild scheduling
  - dirty-region marking for localized graph rebuild queues
  - reachability queries that ignore blocked edges
  - `route_effects` breadth-first route summaries for reachable logistics nodes, including edge
    count, bottleneck capacity, cart speed, animal stamina cost, pathfinding reliability, travel
    safety, corpse recovery, and weather resistance derived from edge quality/capacity
  - node, edge, blocked-edge, and port counts for inspection/tooling

- `SpatialNetworkDeriver`
  - rebuilds networks from complete build-piece ports and validated assembly ports
  - skips incomplete build pieces with exposed ports
  - anchors assembly ports to their source build-piece save ids
  - marks build-piece ports as owned by the build piece and assembly ports as owned by the
    assembly while still retaining the source build-piece anchor
  - creates deterministic rebuildable node/port/edge ids from stable save ids and port names
  - connects co-located same-kind port nodes with local edges

- `NetworkDatabase::rebuild_from_ports`
  - replaces derived network graphs from `BuildObjectDatabase` and `AssemblyDatabase`
  - keeps spatial networks rebuildable derived data instead of authoritative saved data
  - exposes read-only network snapshots so world-level systems can derive runtime views without
    owning network storage

- `Inspector::inspect(SpatialNetwork)`
  - reports network kind, dirty state, graph counts, capacity totals, blocked edges, and
    dirty-region kind
  - warns on empty graphs or blocked edges so derived network issues are visible in tools

Specialized gameplay systems should build on this graph rather than creating custom
one-off adjacency scans.
