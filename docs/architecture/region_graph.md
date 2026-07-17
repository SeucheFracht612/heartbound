# Region Graph

The region graph is the engine-owned broad world structure layer. It is separate from
streamed terrain chunks.

Implemented foundation:

- `RegionDescriptor`
  - stable region id
  - age association
  - biome cluster
  - sub-biome tags
  - resource rules that reference content prototype ids
  - danger and magic gradients
  - future-tool and mastery-return layer tags
  - normalized ecology parameters

- `RegionConnection`
  - links two regions with a connection kind
  - stores traversal cost and capacity
  - rejects self-connections, missing regions, and duplicates

- `RegionGraph`
  - owns region descriptors and broad region connections
  - provides region lookup, connection lookup, adjacency checks, and ecology parameter access

`WorldState` owns one `RegionGraph`, but region descriptors are not terrain voxels and are
not stored in chunk cells. Chunks remain the streamed editable mass layer. Regions describe
worldgen/ecology/danger context. `DeterministicTerrainGenerator` already resolves the configured
region and its resource rules for terrain cells and external generated features; scenarios,
navigation, and outpost systems can consume the same graph as they are implemented.

The current graph is runtime data only. Save snapshots still persist chunk edits and
authored object state; generated region data should remain reproducible from seed and mod
content unless a future cache version needs explicit persistence.
