# Math Architecture

Math is an engine-owned foundation for shared spatial types.

Implemented foundation:

- `Vec3<T>`
  - typed 3D vector with finite checks
  - arithmetic operators
  - dot, cross, length, component min/max, and splat helpers

- `Transform3<T>`
  - position
  - rotation in degrees
  - scale
  - finite and non-zero-scale validation helpers

- `Bounds3<T>`
  - min/max bounds
  - validity checks
  - point containment
  - expansion and merge helpers

- Common aliases
  - `Vec3f`
  - `Vec3d`
  - `Coord3i`
  - `Coord3u16`
  - `Transform3f`
  - `Transform3d`
  - `Bounds3i`
  - `Bounds3f`
  - `Bounds3d`

- `Mat4f` and `Vec4f`
  - column-major storage and column-vector multiplication matching the default GLSL layout
  - matrix/matrix and matrix/vector multiplication
  - translation, scale, Euler rotation, transform, bounds-transform, Vulkan-depth perspective,
    view, and camera-direction helpers
  - finite checks for data passed to rendering

Build pieces and entities retain the shared `Vec3d` alias, but their authoritative `Transform`
aliases `world::WorldTransform`: an exact signed-64-bit block anchor plus a normalized local offset,
with double-precision rotation and scale. This prevents large-world positions from collapsing into a
single imprecise global floating-point coordinate. Physics uses the shared float vector type through
`physics::Vec3`.

This layer should remain small and dependency-light. Renderer, physics, world, save,
debug, and gameplay code should depend on these stable primitives instead of inventing
private vector and transform types for every subsystem. Domain-specific coordinate
types such as voxel coordinates and workpiece cells can remain separate when their
integer ranges or invariants are meaningful.
