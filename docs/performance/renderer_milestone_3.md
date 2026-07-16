# Renderer Milestone 3: instrumentation and benchmark foundation

Milestone 3 is the renderer's measurement baseline. It separates owner-thread work, worker meshing,
GPU execution, transfer work, and synchronization waits without assigning a delayed GPU sample to
the CPU frame that happened to receive it.

## CPU measurements

`ScopedCpuTimingZone` and `ScopedCpuTimer` use `std::chrono::steady_clock`. A recorder accumulates
multiple occurrences of the same zone during one renderer frame. `RendererStats` exposes:

| Measurement | Scope |
| --- | --- |
| `cpu_frame_ms` | successful `synchronize_chunks()` plus extraction, frame construction, and backend execution |
| `render_extraction_ms` | retained-scene visibility and draw extraction |
| `chunk_synchronization_ms` | snapshot/result/upload/scheduling work on the renderer owner thread |
| `culling_ms` | distance and frustum tests |
| `draw_list_ms` | visible terrain draw-packet construction and sorting |
| `command_build_ms` | frame-plan and pass-command construction only; draw-list time is not double-counted |
| `command_recording_ms` | backend command validation/recording |
| `chunk_snapshot_ms` | immutable neighborhood snapshot creation |
| `meshing_ms` | elapsed worker time for results drained in this frame |
| `upload_preparation_ms` | compact GPU-vertex conversion and upload packet preparation |
| `upload_ms` | owner-thread upload submission/preparation |
| `gpu_wait_ms` | fence, acquire, queue-idle, and device-idle waits performed by frame execution |

Worker meshing time is work attribution, not main-thread blocking time. With concurrent workers its
sum can exceed `cpu_frame_ms`. GPU upload timing is independent of the CPU `upload_ms` field.

## GPU measurements

The Vulkan backend uses timestamp queries without `VK_QUERY_RESULT_WAIT_BIT`. Three frame slots
contain eight timestamps each: complete frame, opaque terrain, frame transfer, and final copy.
Completed queries are polled after normal frame-context completion and report both
`gpu_timing_frame_index` and `gpu_timing_latency_frames`.

Each asynchronous upload context owns a separate two-query pool. A completed sample reports
`gpu_upload_submission_serial`, so upload work is not mislabeled as current-frame work. Unsupported
timestamp queues leave the validity flags false.

`VK_EXT_debug_utils` is enabled independently of the validation layer when available. Command
labels cover opaque terrain, frame transfer, final copy, batched buffer upload, and sampled-image
upload. Cutout and transparent intervals will receive distinct queries when those passes exist.

## Counters and reporting

Per-frame statistics include loaded, pending-mesh, pending-upload, resident, visible, culled, and
drawn chunk counts; draw/pipeline counts; vertices and triangles; texture and terrain bytes; arena
usage/fragmentation; pending upload bytes; and bytes uploaded in the frame. `format_renderer_stats()`
provides a compact development/log line. Benchmark JSON and CSV retain the complete timing and
counter record for every measured frame.

The benchmark summary includes median, p95, p99, maximum, 1% low, and 0.1% low results; independent
means for every CPU/GPU interval; uploaded bytes; and the complete CPU timing breakdown of the
slowest frame. GPU means include only samples whose validity flag is true.

## Deterministic workloads

`heartstead_render_benchmark --list-scenes` exposes:

- `flat`
- `mountains`
- `caves`
- `checkerboard`
- `forest`
- `rapid-edits`
- `flythrough`
- `churn`
- `large-coordinates`
- `resize-minimize`

Terrain uses integer coordinate hashes and an explicit seed. Dynamic edits, churn, camera motion,
and resize phases are indexed by simulation frame. Initial chunks settle to resident meshes before
warm-up begins. Rendering is uncapped when `--frame-cap` is zero, which is the default.

JSON uses schema `heartstead.renderer_benchmark.v1`. Both export formats record the scene, seed,
backend, mesher, initial resolution, radius, warm-up/measured frame counts, frame cap, and validation
request. CSV repeats the aggregate frame-time summary on each row for self-contained tabular use.

## Verification snapshot

Verified on 2026-07-16 with:

- AMD Ryzen 7 5800U, 8 cores / 16 threads
- AMD Radeon Graphics (RADV RENOIR, Mesa 25.2.8)
- GCC 13.3.0 and CMake 3.28.3
- Debug build at 1280x720

Repository verification:

```bash
cmake --build build/default-debug -j2
ctest --test-dir build/default-debug --output-on-failure
```

All 50 tests passed. The benchmark runner also completed all ten scenes headlessly with four
measured frames each; every file parsed as the v1 schema and contained the requested frame count.

Native delayed-frame verification:

```bash
build/default-debug/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene rapid-edits --radius 1 --warmup 8 --frames 24 \
  --output build/benchmarks/m3-native-audit.json
```

All 24 measured frames carried valid GPU timings with one-frame latency. The intervals for complete
frame, opaque terrain, transfer, and final copy were nonnegative.

Native upload verification:

```bash
build/default-debug/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene churn --radius 0 --warmup 0 --frames 32 --format csv \
  --output build/benchmarks/m3-native-upload-audit.csv
```

The run produced 32 delayed frame samples and four upload samples. Every CSV row had the same 64
columns, including the upload submission serial and timing validity fields.

`VK_LAYER_KHRONOS_validation` was not installed on this host. Validation requests therefore emitted
the intended availability warning and continued; the native logs contained no renderer error,
VUID, or debug-messenger diagnostics. A validation-layer run remains part of the normal verification
command on a host or CI image that provides the layer.

