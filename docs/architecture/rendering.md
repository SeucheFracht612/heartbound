# Rendering Architecture

Rendering is an engine-owned system behind a small RHI boundary.

Implemented foundation:

- `RenderBackend`
  - names supported backend slots: `headless` and `vulkan`
  - lets tools and samples query whether a backend is currently available

- `IRenderDevice`
  - owns backend-specific frame execution
  - exposes `RenderDeviceCapabilities`
  - exposes current output extent and completed frame count
  - validates resize and frame output extents
  - returns per-frame stats for smoke tests and debug tooling
  - executes validated `RenderFramePlan` records as the frame-level renderer contract
  - owns opaque renderer resource handles for uploaded buffers and sampled images
  - can upload renderer-neutral CPU data, such as chunk mesh vertices, indices, and small RGBA8
    sampled images, without exposing backend handles to world/chunk/material systems
  - can create shader modules from validated SPIR-V words without exposing backend shader handles
  - can create compute pipeline objects from shader modules and bound material pipeline layouts
    without exposing backend pipeline handles
  - can create graphics pipeline objects from vertex/fragment shader modules and bound material
    pipeline layouts without exposing backend pipeline handles
  - can write descriptor bindings for material scalar/color uniform buffers and sampled texture
    images without exposing backend descriptor set handles
  - can structurally bind uploaded mesh buffers to material prototype ids without exposing backend
    pipelines, descriptors, or Vulkan handles
  - reports whether mesh draw binding also submitted backend draw commands, keeping command
    execution observable without exposing command buffers
  - builds renderer-neutral frame execution plans before smoke execution, so future Vulkan barriers
    and image-layout transitions can be derived from the public RHI contract rather than hidden
    backend inference
  - can structurally bind material pipeline layouts, including shader template and descriptor slots,
    without exposing Vulkan pipeline layouts or descriptor sets through the public RHI

- `RenderDeviceDesc`
  - selects backend, application name, initial extent, present mode, validation, and an optional
    platform-native window handle
  - lets the Vulkan backend create a private surface without exposing Vulkan or Xlib types through
    the public RHI

- `RenderDeviceCapabilities`
  - reports backend, maximum extent, present support, validation support, debug marker support,
    shader-module support, pipeline layout support, compute-pipeline support, graphics-pipeline
    support, descriptor-write support, buffer upload support, image upload support, draw binding
    support, and whether the device is headless
  - gives future Vulkan setup, diagnostics, and tests a stable capability query without exposing
    backend handles

- `RenderBackendCapabilities`
  - reports backend availability before a device is created
  - describes present, validation, debug marker, shader-module, pipeline-layout,
    compute-pipeline, graphics-pipeline, descriptor-write, buffer-upload, image-upload,
    draw-binding, window-surface, GPU-device, headless, frames-in-flight, and graphics API
    requirements
  - lets tools inspect the Vulkan contract even on machines where the optional backend is
    unavailable

- `RenderFramePlan`
  - declares named render resources and ordered render passes
  - validates resource names, pass names, extents, duplicate declarations, read-before-write
    hazards, duplicate pass resource references, and unambiguous present-pass rules
  - compiles validated plans into a renderer-neutral execution plan with ordered pass names,
    per-resource access records, and explicit resource dependency edges for write/read,
    write/write, read/write, and present transitions
  - derives renderer-neutral resource states and transition records, including `undefined`,
    `external`, `shader_read`, color attachment write/read-write, and `present` states, so backend
    synchronization can be tested without exposing Vulkan enums
  - models clear, world, post-process, UI, debug, and present pass kinds without exposing Vulkan
    objects
  - can be submitted to an `IRenderDevice` for smoke execution, with backend frame stats preserving
    the validated pass, resource-use, dependency, transition, planned synchronization-barrier, and
    backend-submitted synchronization-barrier counts
  - gives Vulkan, shader-pack extensions, and debug tooling a tested pass contract before the
    production renderer exists

- Renderer-neutral chunk mesh data
  - `ChunkMesher` extracts CPU-side terrain surface meshes outside the renderer backend
  - render backends can later upload this data without making chunks depend on Vulkan objects
  - the smoke RHI can upload chunk mesh vertex and index buffers as renderer-owned resources before
    a production draw pipeline exists
  - mesh draw binding validates vertex/index resource usage and material ids while keeping chunks
    independent from renderer handles

- `MaterialDefinition` and `MaterialRegistry`
  - describe renderer-facing material data with prototype ids, domains, blend modes, shader
    templates, texture bindings, scalar parameters, and color parameters
  - validate virtual paths, finite numeric values, color ranges, and duplicate parameter names
  - can be built from `material` prototypes loaded through the normal mod content pipeline
  - validate declared shader templates and texture bindings against active asset catalog records
  - can produce structural RHI pipeline layouts from shader template, texture, scalar, and color
    declarations
  - keep material and shader-pack data above backend handles so mods and resource packs can use
    declared templates instead of raw Vulkan access

- `ShaderCompiler`
  - consumes active shader assets from the asset catalog, after mod/resource-pack override rules
    have selected the winning source
  - infers source language (`slang`, `hlsl`, or SPIR-V passthrough) and role (`template`,
    `vertex`, `fragment`, `compute`, or `library`) from the virtual source path
  - writes a deterministic development shader manifest and compiled payload wrappers
  - supports a production profile for validated SPIR-V passthrough payloads
  - reports explicit diagnostics when production Slang/HLSL compilation is requested before a real
    compiler backend is linked
  - exposes structured inspection for compiled shader records and compile results through the
    engine inspector and `heartstead_shader_compiler --inspect`
  - gives shader packs a controlled validation/cook boundary before real Slang-to-SPIR-V
    integration exists

- Headless render backend
  - compiles everywhere
  - validates renderer lifetime, resize, clear color, frame indexing, present intent, and the
    default clear/present frame plan
  - executes arbitrary validated frame plans deterministically for tests and tools
  - reports deterministic capabilities for present, validation, debug markers, buffer upload, and
    pipeline layout binding, draw binding, and max extent
  - records uploaded buffer and image handles without pretending to own GPU memory
  - validates material pipeline layouts and descriptor slot declarations without pretending to own
    GPU pipeline layouts
  - validates uniform descriptor writes against bound material layouts, uploaded uniform buffers,
    and byte ranges
  - validates sampled texture descriptor writes against bound material layouts and uploaded image
    resources
  - validates mesh draw bindings against uploaded buffer usage, material prototype ids, and
    material graphics pipeline availability without submitting GPU commands
  - gives tests and CI a renderer path before OS windows or Vulkan are required

- Vulkan backend boundary
  - exists as an explicit backend factory
  - is compiled when CMake finds the Vulkan SDK/loader and `HEARTSTEAD_ENABLE_VULKAN` is enabled
  - creates a Vulkan instance, selects a physical device with a graphics queue, and creates a
    logical device for smoke validation
  - can create and own an X11 `VkSurfaceKHR` from a platform-native window handle when the optional
    X11 backend is compiled
  - owns a private command pool, command buffer, fence, sync semaphores, offscreen color image/view,
    and optional swapchain for submitted clear-frame and draw-command smoke work
  - can acquire, clear, and present a surface-backed swapchain image when a native window is
    supplied; the offscreen path remains available for headless development and CI
  - can allocate host-visible Vulkan buffers and copy renderer-neutral upload bytes into them behind
    opaque RHI handles
  - can allocate private `VkShaderModule` objects from validated SPIR-V words behind opaque RHI
    handles
  - can upload small RGBA8 sampled images through a private staging buffer, optimal-tiled image,
    image view, and sampler behind opaque RHI handles
  - can allocate private compute `VkPipeline` objects from owned compute shader modules and bound
    material pipeline layouts
  - can allocate private graphics `VkPipeline` objects from owned vertex/fragment shader modules
    and bound material pipeline layouts through a smoke render pass
  - can update private material descriptor sets for uniform scalar/color bindings from uploaded
    uniform buffers and sampled texture bindings from uploaded sampled images
  - can submit a minimal offscreen draw command buffer that binds a material graphics pipeline,
    uploaded vertex/index buffers, dynamic viewport/scissor state, and indexed/non-indexed draw
    calls against a private framebuffer
  - can execute dependency- and transition-planned frame plans structurally while the smoke backend
    maps them onto its existing clear/offscreen/present primitives
  - translates planned RHI resource states into private Vulkan image layouts, access masks, and
    pipeline stages before smoke execution
  - allocates temporary Vulkan frame images for non-target offscreen frame resources and records
    their planned `VkImageMemoryBarrier`s before the existing clear primitive
  - records planned `VkImageMemoryBarrier`s for the smoke-owned swapchain target on present frames,
    making submitted barrier counts observable separately from the full frame-plan synchronization
    count without exposing Vulkan handles through the RHI
  - can allocate private `VkDescriptorSetLayout`, `VkPipelineLayout`, `VkDescriptorPool`, and
    descriptor set objects for material pipeline layouts without exposing their handles
  - requires a material pipeline layout before mesh draw binding, so future draw execution has a
    validated material-to-descriptor contract
  - can validate mesh draw bindings against uploaded Vulkan buffer resources, material prototype
    ids, and material graphics pipeline availability before submitting the offscreen draw smoke
  - reports unavailable when Vulkan is not compiled in or no graphics-capable physical device is
    present
  - does not expose Vulkan handles through the RHI

The renderer must stay below gameplay, modding, save, and simulation systems. Gameplay
code should not depend on Vulkan types, swapchain objects, descriptor handles, or
backend-specific allocation details. Future render features should enter through
engine-owned abstractions such as material definitions, asset handles, render passes,
debug draw, and validated shader-pack extension points.

The current Vulkan backend is a smoke backend, not the final renderer. It proves loader,
instance, optional X11 surface, physical-device, logical-device, command submission,
synchronization, offscreen clear-image lifetime, basic swapchain clear/present, host-visible buffer
upload lifetime, private Vulkan shader-module creation, dependency- and transition-planned
frame-plan execution with private Vulkan synchronization translation and offscreen all-resource
barrier submission, private Vulkan descriptor-layout/pipeline-layout allocation, descriptor set
allocation, private compute pipeline creation, RGBA8 sampled image upload, uniform and sampled
texture descriptor writes, and private graphics pipeline creation behind the RHI, plus minimal
offscreen draw command submission from validated mesh bindings. The smoke backend now distinguishes
total planned synchronization barriers from backend-submitted barriers, and offscreen smoke frames
submit planned barriers for all declared transient frame resources through temporary backend-owned
images.
Production submission of frame-plan transition records as full Vulkan render-pass/subpass
execution, descriptor lifetime policy for material-bound shaders, broader staged GPU upload
policy, compressed texture/KTX2 handling, and RenderDoc capture workflow still belong to later
Vulkan integration slices.

The current shader compiler has development validators plus a production SPIR-V passthrough
profile. Development preserves source bytes behind explicit compiled-shader metadata and rejects
empty or unsupported shader sources. Production currently accepts only `.spv` assets that pass
basic SPIR-V header validation and reports `shader_compiler.production_compiler_unavailable` for
Slang/HLSL source until a real compiler backend is linked. Future production shader compilation
must keep this declared asset/profile boundary without allowing mods or resource packs to bypass
declared shader templates and render extension points.
