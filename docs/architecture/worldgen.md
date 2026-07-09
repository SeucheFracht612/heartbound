# World Generation

World generation creates baseline terrain chunks from seed, region descriptors, and the
voxel palette. It is separate from player edit storage.

Implemented foundation:

- `TerrainGenerationConfig`
  - world seed
  - region id
  - signed 64-bit base surface height
  - deterministic surface variation

- `DeterministicTerrainGenerator`
  - resolves the requested region from `RegionGraph`
  - selects a terrain voxel through the region's resource rules and `VoxelPalette`
  - creates a `VoxelChunk` deterministically from seed and chunk coordinate
  - uses checked chunk/local-to-block conversion and rejects chunks whose cell extent cannot be
    represented by signed 64-bit `BlockCoord`
  - marks generated chunks dirty for mesh, collision, and lighting rebuilds
  - does not mark generated chunks dirty for save or replication

- `ChunkDatabase::insert_generated`
  - inserts generated chunks without creating voxel edit records
  - rejects overwriting an existing chunk
  - can emit rebuild dirty regions for generated chunks

This preserves the save model boundary:

```text
generated terrain = reproducible from seed/mod content/region descriptors
player edits      = explicit chunk edit records
```

Future work should replace the simple heightfield with real biome/resource feature
generation, but it should keep the same boundary: worldgen produces baseline chunks, and
long-term saves persist deltas plus enough version/prototype information to migrate them.
