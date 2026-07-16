# Renderer V1 performance closure

Recorded on 2026-07-17 after the retained terrain, asynchronous meshing, device-local arenas,
frames-in-flight, runtime assets, greedy mesher, render phases, instanced scene, debug renderer, and
UI bridge were integrated.

## Published target machine

- AMD Ryzen 7 5800U, 8 cores / 16 threads
- AMD Radeon Graphics, RADV RENOIR / Mesa 25.2.8
- 13 GiB usable memory
- Linux Mint 22.3, Linux 6.17.0-40-generic
- GCC 13.3.0, Release configuration
- 1920x1080, Vulkan immediate present mode, uncapped

The benchmark now accepts `--width` and `--height`, records those dimensions in its metadata, and
keeps both CPU and delayed GPU measurements. The validation layer was requested but was not
installed on this machine; the intended warning was emitted and no Vulkan error or debug-messenger
diagnostic was produced.

## Reproduction

```sh
cmake --preset default-release
cmake --build --preset default-release -j2 --target heartstead_render_benchmark

build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene mountains --radius 4 --width 1920 --height 1080 \
  --warmup 60 --frames 300 --output build/benchmarks/m13/native-mountains-1080p.json

build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene churn --radius 4 --width 1920 --height 1080 \
  --warmup 60 --frames 300 --output build/benchmarks/m13/native-churn-1080p.json

build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene rapid-edits --radius 4 --width 1920 --height 1080 \
  --warmup 60 --frames 300 --output build/benchmarks/m13/native-rapid-edits-1080p.json

build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --vulkan --scene flat --radius 8 --width 1920 --height 1080 \
  --warmup 60 --frames 300 --output build/benchmarks/m13/native-flat-radius8-1080p.json
```

The radius-eight flat run loads a 17x17 chunk field. Distance and frustum rejection retain 289 GPU
meshes, select 194 visible chunks, and submit 411 section draws. The mountainous scene uses a
smaller field but exercises 439,462 visible triangles rather than the intentionally minimal flat
geometry.

## Results

| Workload | Median | p95 | p99 | 1% low | 0.1% low | Maximum | Mean GPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mountains run 1, radius 4 | 1.762 ms | 2.801 ms | 2.852 ms | 335.8 FPS | 320.7 FPS | 3.118 ms | 1.442 ms |
| Mountains run 2, radius 4 | 1.775 ms | 2.809 ms | 2.895 ms | 342.0 FPS | 336.5 FPS | 2.972 ms | 1.440 ms |
| Streaming churn, radius 4 | 0.840 ms | 1.744 ms | 1.908 ms | 498.5 FPS | 459.7 FPS | 2.175 ms | 0.538 ms |
| Rapid edits, radius 4 | 3.791 ms | 4.377 ms | 4.785 ms | 172.1 FPS | 157.5 FPS | 6.349 ms | 0.859 ms |
| Flat 17x17 field, radius 8 | 0.894 ms | 1.872 ms | 2.019 ms | 448.0 FPS | 419.4 FPS | 2.384 ms | 0.593 ms |

The repeated mountainous medians differ by 0.70%; p95 differs by 0.30%, and mean GPU time differs
by 0.16%. That is sufficiently repeatable for before/after renderer decisions on this machine.

## Bottleneck decision

Steady mountainous rendering is GPU-paced: mean GPU time is 1.44 ms and mean CPU time is 1.91 ms,
of which about 1.57 ms is waiting when a frame context is reused. Culling is 0.016 ms, command
building is 0.016 ms, and recording is 0.13 ms, so GPU-driven submission or a more complex spatial
index is not justified.

Rapid edits are snapshot-bound, not upload- or render-bound:

- immutable neighborhood snapshot construction: 3.338 ms mean;
- completed worker meshing attribution: 2.150 ms mean;
- owner-thread upload submission: 0.015 ms mean;
- GPU frame: 0.859 ms mean;
- GPU wait: 0.014 ms mean.

The rapid-edit stress case still meets the provisional gate: median is below 6.94 ms, 1% low is
above 100 FPS, and the maximum measured frame is below 6.94 ms. Snapshot work already has a fixed
per-frame cell budget and old meshes remain resident, so no speculative job graph, GPU culling, or
additional transfer queue was added. Future optimization should start with snapshot copying only if
gameplay produces a worse measured edit workload.

## Closure

The measured workloads satisfy the Renderer V1 target on the published machine. Normal steady
frames perform no shader compilation, pipeline creation, per-chunk `vkAllocateMemory`, immediate
submission fence wait, per-chunk descriptor allocation, synchronous disk access, or synchronous
main-thread meshing. Pipeline/resource counts remain stable after warm-up, streaming remains above
60 FPS, and the retained GPU byte counters do not grow in steady scenes.

These numbers are a baseline for regression testing, not a universal hardware guarantee. Any
future renderer complexity requires a captured workload that fails an agreed gameplay target.
