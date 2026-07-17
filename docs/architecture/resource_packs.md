# Resource Pack Architecture

Resource packs are presentation-focused unless they are also gameplay mods.

Implemented foundation:

- resource pack manifests under `resource_packs/*/resource_pack.toml`
- namespace-style pack ids
- strict bounded flat-manifest parsing with duplicate-key, unknown-field, boolean, and extension
  diagnostics
- deterministic resource-pack load plans with explicit per-pack asset priorities
- asset directory mounting through the virtual file system
- safe text and binary asset reads through virtual paths, with later mounts overriding earlier
  mounts in the same namespace
- active virtual directory listing that reports each visible file once after resource-pack
  overrides are applied
- asset catalog indexing with resource-pack priority over mod assets, plus VFS-backed active
  overlay indexing for the final visible asset set
- one policy-owned indexing path shared by content validation, cook tools, shader compilation, and
  the sandbox so restricted files cannot bypass validation in a secondary consumer
- material definitions with validated virtual paths for shader templates, texture bindings,
  scalar parameters, and color parameters
- active asset validation so resource-pack texture overrides can satisfy material bindings without
  changing gameplay prototype identity
- shader compilation uses the active shader asset selected by the catalog, so shader-pack
  overrides stay inside a declared validation/cook step
- shader overrides must declare one of the engine-owned `shader_extensions` in the manifest and
  live below `shaders/extensions/<extension>/`; undeclared/unscoped shaders and raw Vulkan/SPIR-V
  injection are rejected before content loading

Resource packs should replace textures, models, sounds, fonts, localization, UI skins,
material parameters, and shader presets through virtual paths. Engine and gameplay code
should not hardcode real disk paths.
