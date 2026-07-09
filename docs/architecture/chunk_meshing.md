# Chunk Meshing

Chunk meshing converts terrain voxels into a renderer-neutral surface mesh. It does not
own voxel storage, renderer buffers, Vulkan handles, materials, or gameplay meaning.

Implemented foundation:

- `ChunkMeshVertex`
  - local position
  - face normal
  - UVs
  - source voxel type
  - source voxel light

- `ChunkMesh`
  - source chunk coordinate
  - local bounds
  - vertices
  - uint32 indices
  - face count
  - validation for counts, bounds, finite vertices, and index ranges

- `ChunkMesher::build_surface_mesh`
  - emits faces for solid voxel sides exposed to air or chunk boundaries
  - hides internal faces between adjacent solid voxels
  - returns an empty valid mesh for fully air chunks

This is intentionally a CPU-side extraction contract. Future renderer work should upload
`ChunkMesh` data into backend buffers, and future meshing work can add greedy meshing,
cross-chunk occlusion, material buckets, and async rebuild jobs without changing chunk
storage into renderer data.
