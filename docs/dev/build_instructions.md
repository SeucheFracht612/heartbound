# Build Instructions

Configure:

```bash
cmake --preset default-debug
```

Build:

```bash
cmake --build --preset default-debug
```

Test:

```bash
ctest --preset default-debug
```

The default CTest preset runs the unit tests and headless-safe smoke tests for the default-built
samples and tools. The commands below are still useful when you want to run one boundary manually.

Strict warning build:

```bash
cmake --preset default-debug-werror
cmake --build --preset default-debug-werror
ctest --preset default-debug-werror
```

AddressSanitizer plus UndefinedBehaviorSanitizer build:

```bash
cmake --preset linux-clang-asan
cmake --build --preset linux-clang-asan
ctest --preset linux-clang-asan
```

ThreadSanitizer build:

```bash
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan
ctest --preset linux-clang-tsan
```

The sanitizer presets use Clang and disable Vulkan so the checks focus on Heartstead-owned
foundation code instead of graphics driver behavior. Use `linux-clang-asan` for memory and
undefined-behavior checks. Use `linux-clang-tsan` separately for data-race checks; it cannot be
combined with AddressSanitizer. The ASan CTest preset disables LeakSanitizer detection because
managed/debugger-style environments can run tests under `ptrace`, where LeakSanitizer aborts at
startup. Run individual ASan binaries outside that environment with leak detection enabled when
leak-specific investigation is needed.

Run the development asset cooker:

```bash
./build/default-debug/tools/asset_cooker/heartstead_asset_cooker
./build/default-debug/tools/shader_compiler/heartstead_shader_compiler
```

Print cooked asset store inspection data while cooking, or inspect an existing cooked output:

```bash
./build/default-debug/tools/asset_cooker/heartstead_asset_cooker . build/cooked_assets/asset_manifest.txt development --inspect
./build/default-debug/tools/asset_cooker/heartstead_asset_cooker --inspect-store build/cooked_assets
```

Print shader compile inspection data:

```bash
./build/default-debug/tools/shader_compiler/heartstead_shader_compiler . build/compiled_shaders/shader_manifest.txt development --inspect
```

Run the production shader profile for validated SPIR-V passthrough:

```bash
./build/default-debug/tools/shader_compiler/heartstead_shader_compiler . build/compiled_shaders/production_shader_manifest.txt production
```

Run smoke samples:

```bash
./build/default-debug/samples/platform_smoke/heartstead_platform_smoke
./build/default-debug/samples/renderer_smoke/heartstead_renderer_smoke
./build/default-debug/samples/physics_sandbox/heartstead_physics_sandbox
./build/default-debug/samples/network_sandbox/heartstead_network_sandbox
./build/default-debug/samples/scripting_sandbox/heartstead_scripting_sandbox
./build/default-debug/samples/jobs_sandbox/heartstead_jobs_sandbox
./build/default-debug/samples/math_sandbox/heartstead_math_sandbox
./build/default-debug/samples/world_state_sandbox/heartstead_world_state_sandbox
```

Run mod/prototype validation tools:

```bash
./build/default-debug/tools/mod_validator/heartstead_mod_validator
./build/default-debug/tools/mod_validator/heartstead_mod_validator . --inspect
./build/default-debug/tools/prototype_inspector/heartstead_prototype_inspector
./build/default-debug/tools/prototype_inspector/heartstead_prototype_inspector base:items/raw_clay
./build/default-debug/tools/replay_inspector/heartstead_replay_inspector tests/fixtures/sample_command_replay.txt
./build/default-debug/tools/save_inspector/heartstead_save_inspector tests/fixtures/minimal_save_snapshot.txt
./build/default-debug/tools/save_inspector/heartstead_save_inspector --slots tests/fixtures/save_slots
./build/default-debug/tools/shader_compiler/heartstead_shader_compiler
./build/default-debug/tools/workpiece_inspector/heartstead_workpiece_inspector tests/fixtures/sample_workpiece_grid.txt
./build/default-debug/tools/world_inspector/heartstead_world_inspector tests/fixtures/minimal_save_snapshot.txt .
```

The `linux-clang-debug` and `linux-gcc-debug` presets pin specific compilers for CLion
profiles. Use `default-debug` when the host default compiler is preferred.
