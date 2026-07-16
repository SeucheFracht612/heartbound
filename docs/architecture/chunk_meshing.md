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
  - has a worker-safe overload that consumes only `ChunkNeighborhoodSnapshot` and
    `BlockRenderTableSnapshot`

- Immutable worker input
  - `ChunkNeighborhoodSnapshot` stores contiguous center-plus-halo cells; ordinary cube visibility
    uses a 34 x 34 x 34 snapshot for a 32-cubed chunk
  - dependency records cover both present and absent neighboring chunks and retain load generation
    plus content revision for every required chunk
  - `BlockRenderTableSnapshot` resolves voxel definitions and block models into immutable,
    contiguous lookup data before a worker enters the cell loop
  - snapshot buffers are reused by `ChunkMeshScheduler` and snapshot copying is bounded per frame

- Asynchronous result contract
  - every request and result carries `ChunkIdentity`, center revision, dependency revisions,
    block-render-table revision, and scheduling priority
  - a fixed worker pool coalesces duplicate chunk work, limits concurrency, and observes
    cancellation before meshing begins
  - completed meshes cross a mutex-protected typed mailbox and are drained only on the renderer
    owner thread
  - center edits, neighbor edits, unload/reload generation changes, and render-table changes reject
    old results before upload; the newest revision is requeued
  - dirty state is cleared only after a validated replacement is resident

This remains a CPU-side extraction contract. Workers must never retain or query mutable world or
chunk objects. Future meshing work can add greedy meshing and material buckets without changing
chunk storage into renderer data or weakening the snapshot boundary.
