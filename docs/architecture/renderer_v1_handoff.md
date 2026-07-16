# Renderer V1 gameplay handoff

Renderer V1 is an engine-owned retained subsystem. Gameplay owns simulation/world state and sends
presentation updates; it never receives Vulkan handles, command buffers, descriptor sets, GPU
allocation ranges, pipeline objects, or mutable renderer caches.

## Public boundary

The stable frame-facing types are:

- `RenderSceneUpdate` for generation-safe retained object/light upsert and removal;
- `ChunkRenderUpdate` for generation-safe loaded/evicted chunk-instance changes;
- `RenderFrameInput` for camera, simulation interpolation alpha, and frame delta;
- `RenderFrameResult` for the backend frame result plus the complete renderer statistics snapshot;
- `Renderer::resize()` for nonzero framebuffer extents;
- `DebugRenderer` and `UiRenderer` for development/gameplay visualization above the RHI.

`synchronize_chunks(WorldState&, camera)` is the owner-thread extraction boundary for Renderer V1.
It builds immutable center-plus-halo requests and revision stamps. Meshing workers never retain or
query `WorldState`, `ChunkDatabase`, `VoxelChunk`, prototype registries, or mutable block tables.
The direct synchronization entry point can later be replaced by richer presentation change sets
without changing worker, cache, draw, or RHI contracts.

## Ownership and threads

The thread that successfully calls `Renderer::initialize()` is the renderer owner thread;
`is_owner_thread()` is available for integration assertions. Initialization, world synchronization,
scene updates, mesh creation/release, UI submission, rendering, resize, shader reload, and shutdown
belong to that thread.

Chunk-mesh workers receive immutable request values and return typed results through the scheduler
mailbox. They do not perform RHI calls. `DebugRenderer` primitive submission is the sole explicitly
thread-safe presentation submission path; timed/one-frame primitives are merged on the owner thread.
All other cross-thread producers must queue updates to the renderer owner.

## Frame lifecycle

1. Gameplay applies batched scene and chunk load/eviction updates.
2. The owner synchronizes chunk state, constructs bounded immutable snapshots, drains completed mesh
   results, validates every dependency revision, and submits budgeted uploads.
3. `render_frame()` extracts camera-relative terrain and object visibility, builds opaque, cutout,
   rich/static, transparent/fluid, debug, and UI draw lists, then creates one validated frame
   submission.
4. Vulkan reuses the next frame context only after its fence completes, resets its local command
   resources, acquires the swapchain image, records one offscreen color/depth render sequence,
   copies to the swapchain, submits once, and presents once.
5. Completion advances the submission serial, releases staging-ring ranges, and collects deferred
   resources whose final referencing serial has completed.

Ordinary frames do not call `vkDeviceWaitIdle()`, compile shaders, create pipelines, allocate one
descriptor set per chunk, or allocate Vulkan memory per chunk. Device idle is reserved for shutdown
and exceptional swapchain recovery.

## Chunk mesh lifecycle

A chunk cache key is `(ChunkCoord, load_generation)`. Content and render-table revisions plus all
neighbor dependency revisions identify the represented mesh. Loaded/dirty chunks move through a
stable priority queue, immutable snapshot, bounded worker scheduler, completed mailbox, pending
upload, and resident arena allocation.

A result is rejected if its chunk unloaded, generation changed, center or neighbor content changed,
render table changed, or a newer revision is already pending/resident. Rejection requeues current
work. The old resident mesh remains visible until both replacement arena allocations and writes
succeed. Empty meshes are valid resident records with no buffer range. Boundary edits carry neighbor
dependency stamps and therefore cannot install an obsolete border.

Eviction and replacement retire vertex/index ranges after the last submitted serial that could
reference them. Free ranges merge when collected. Staging-ring bytes likewise remain owned by the
upload submission until completion.

## Coordinate spaces

Authoritative positions retain an exact signed 64-bit block anchor plus normalized local offset.
The camera owns a `FloatingOrigin`; only a bounded nearby delta is converted to `float`. Terrain
draws push a camera-relative chunk origin, while object instances contain camera-relative matrices.
No global float world position is stored. Matrices are column-major, multiply column vectors, and
use Vulkan zero-to-one clip depth.

## GPU and shader contracts

- `GpuTerrainVertex`: asserted compact 24-byte integer/normalized terrain ABI.
- `GpuStaticMeshVertex`: asserted 32-byte static mesh ABI.
- `GpuObjectInstance`: asserted 96-byte camera-relative instance ABI.
- `GpuDebugVertex`: asserted 28-byte line ABI.
- `GpuUiVertex`: asserted 36-byte screen-space UI ABI.
- `ChunkPushConstants`: asserted 128-byte block containing view-projection, draw origin, sun,
  ambient, and fog lanes.

Every attribute location and integer/float format is explicit in its header and shader interface.
SPIR-V magic, size, stage, entry point, vertex inputs, descriptors, and push constants are validated
before module/pipeline creation. Development reload creates and validates replacements before
retiring the prior valid modules/pipelines. Release gameplay performs no shader compilation.

Terrain materials use one sampled texture array and one contiguous GPU material table. Vertices or
sections carry stable material indices. Chunks do not own pipelines or descriptor sets. UI uses a
two-layer atlas and per-draw validated scissors; debug lines use depth-tested/overlay shared
pipelines.

## Render phase order

The explicit order is sky/background, opaque terrain, alpha-tested terrain, rich/static instances,
transparent terrain/fluids, debug, UI, and present. Opaque/cutout write depth. Transparent/fluid
geometry tests but does not write depth and uses stable back-to-front section ordering. UI always
passes depth without writing it. Terrain lighting is linear; the final unorm scene color is encoded
for sRGB display, and distance fog hides the visible-radius boundary.

## Resize and minimization

The platform forwards nonzero framebuffer changes to both camera aspect ratio and `Renderer::resize`.
Vulkan recreates swapchain-dependent, per-frame offscreen color/depth targets and format-compatible
framebuffers. Terrain/static arena buffers, textures, retained mesh ranges, instance storage, debug
buffers, and UI buffers survive resize. A zero framebuffer extent is treated as minimized by the
application: rendering is skipped until a nonzero resize arrives. Out-of-date/suboptimal acquire or
present triggers swapchain recovery without accepting stale image ownership.

## Budgets and diagnostics

Snapshot cells, concurrent jobs, completed-result drain, mesh count, upload count/bytes, terrain GPU
bytes, instance count, debug lines/text, and UI vertices/indices are fixed configuration budgets.
Exhaustion delays work or reports visible overflow rather than growing without bound. `RendererStats`
separates CPU extraction/synchronization/culling/build/record/wait, worker meshing, uploads, delayed
GPU passes/copy, residency/arena usage, objects/instances, debug, and UI counters. Deterministic
benchmark JSON/CSV plus the development log formatter expose the same record.

The published target and reproduction commands are in
`docs/performance/renderer_milestone_13.md`.

## Failure behavior

- stale/invalid draw packets and scissors are rejected before backend execution;
- missing or stale texture/mesh handles resolve to visible error assets;
- failed shader hot reload keeps the prior valid program and pipelines;
- initial required shader-set load failure is a visible startup error (there is no hidden release
  shader compiler);
- failed replacement allocation/upload preserves the old resident mesh and dirty revision;
- staging or upload-budget exhaustion delays work;
- invalid/stale scene and chunk generations cannot remove newer records;
- shutdown drains workers, waits for device completion, and releases all Vulkan resources.

Vulkan device-loss recovery is explicitly unsupported in Renderer V1. A device-lost result is a
fatal renderer error requiring application-level renderer/device recreation. This is preferable to
an untested partial recovery path.

## Handoff rule

Renderer V1 is closed. New renderer features require a gameplay requirement or a captured benchmark
that fails the published target. Cascaded shadows, deferred/clustered lighting, SSAO/SSR, volumetrics,
ray tracing, mesh shaders, bindless rendering, GPU occlusion, clipmaps, complex LOD, TAA, and
cinematic post-processing remain deliberately out of scope.
