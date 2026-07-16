# Renderer Milestone 8 baseline

Recorded on 2026-07-16. These measurements compare the production greedy cube path with the
readable reference mesher through the same immutable snapshots, scheduler, retained renderer,
compact vertex conversion, GPU cache, and headless frame validation path.

## Test machine

- CPU/GPU: AMD Ryzen 7 5800U (8 cores/16 threads), integrated Radeon Graphics
- Memory: 13 GiB usable
- OS: Linux Mint 22.3, Linux 6.17.0-40-generic
- Compiler: GCC 13.3.0
- CMake/Ninja: 3.28.3 / 1.11.1

The headless backend was used for the CPU comparison so window-system and GPU scheduling noise do
not enter the meshing result. Native Vulkan was separately smoke-tested on RADV Renoir.

## Reproduction

```sh
cmake --preset default-debug
cmake --build --preset default-debug --target heartstead_render_benchmark
cmake --preset default-release
cmake --build --preset default-release --target heartstead_render_benchmark

./build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --headless --reference-mesher --scene rapid-edits --radius 1 \
  --warmup 16 --frames 120 --output build/benchmarks/m8-reference-release.json

./build/default-release/apps/render_benchmark/heartstead_render_benchmark \
  --headless --scene rapid-edits --radius 1 \
  --warmup 16 --frames 120 --output build/benchmarks/m8-greedy-release.json
```

Repeat with `build/default-debug` for Debug measurements. The benchmark first settles every loaded
chunk to a resident mesh, then begins warm-up and scene mutation. Rapid edits deliberately keep
changing revisions, so the old resident geometry remains visible while stale jobs are rejected.

## Results

| Metric | Reference | Greedy | Change |
|---|---:|---:|---:|
| Resident triangles, nine flat chunks | 43,776 | 108 | -99.75% (405.3x smaller) |
| Resident mesh bytes | 2,363,904 | 5,832 | -99.75% (405.3x smaller) |
| Debug worker meshing | 19.146 ms | 11.193 ms | -41.5% |
| Debug median frame | 34.260 ms | 33.748 ms | -1.5% |
| Release worker meshing | 2.571 ms | 1.890 ms | -26.5% |
| Release median frame | 3.112 ms | 3.053 ms | -1.9% |
| Release p95 frame | 3.494 ms | 3.262 ms | -6.6% |

The full-frame improvement is intentionally smaller than the worker improvement: this stress scene
also spends time constructing immutable snapshots and processing continuous dirty-region churn.
The geometry and byte reductions directly reduce later upload, vertex-processing, and residency
costs. Exact frame percentiles vary with host load; generated JSON should be retained when comparing
future changes.

