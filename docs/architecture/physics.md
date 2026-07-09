# Physics Architecture

Physics is an engine-owned integration boundary. Gameplay objects may create and own
physics bodies through engine APIs, but physics body ids are runtime ids, not permanent
save identity and not entity ids.

Implemented foundation:

- `PhysicsBackend`
  - names backend slots: `headless` and `jolt`
  - lets tests, tools, and samples query backend availability

- `PhysicsBackendCapabilities`
  - reports backend availability before world creation
  - describes deterministic stepping, dynamic/kinematic/static bodies, compound shapes,
    AABB queries, contacts, sleeping, character controllers, constraints, collision response,
    and backing library
  - lets gameplay and tooling understand the Jolt contract without owning Jolt handles

- `IPhysicsWorld`
  - creates and destroys runtime physics bodies
  - exposes body state through `PhysicsBodyId`
  - exposes backend capability metadata from the created world
  - validates shape, mass, position, velocity, gravity, and timestep inputs
  - steps the world through an explicit timestep
  - supports broad-phase AABB overlap queries
  - drains per-step contact records for debug, gameplay, and tests

- Shape descriptors
  - box
  - sphere
  - capsule
  - compound shapes with child shapes

- `PhysicalResourceRecord`
  - uses one compound physics body for felled trees and other large physical resources
  - stores stable resource identity separately from runtime physics body ids
  - moves from cutting, dynamic, settled, frozen, and cargo-converted lifecycle states

- Motion types
  - static bodies for terrain/building collision
  - kinematic bodies for controlled movement
  - dynamic bodies for simulated objects

- Headless backend
  - compiles everywhere
  - integrates dynamic and kinematic bodies deterministically
  - supports impulses for dynamic bodies
  - computes conservative shape AABBs for boxes, spheres, capsules, and compounds
  - reports overlapping body pairs as contact records
  - applies deterministic AABB positional correction for dynamic contacts
  - removes velocity into the contact normal for simple non-bouncy collision response
  - sleeps settled dynamic bodies after repeated low-velocity contact steps

- Jolt backend boundary
  - exists as an explicit backend factory
  - reserves full collision response, sleeping, character controllers, constraints, broad-phase
    queries, contacts, and compound shapes behind the engine API
  - currently reports `physics.jolt_unavailable`
  - will own the external rigid-body library integration later

This layer is deliberately separate from `engine/entities/`, `engine/world/`, and
`engine/save/`. A saved entity, build piece, cargo object, or felled tree can reference
its own stable save id while recreating physics bodies when loaded. Save files must not
store raw physics ids or backend handles as permanent identity.

The current headless backend is not a replacement for full rigid-body response,
character controllers, constraints, or Jolt integration. It is the testable contract for
runtime body ownership, validation, stepping, compound-body representation, broad-phase
queries, contact event plumbing, deterministic dynamic-body correction, and sleeping state.
