# Heartstead Engine v0.2 implementation audit and closure report

## Implementation closure — 2026-07-10

The findings below are preserved as the pre-implementation baseline. The v0.2 engine pass has now
closed the architectural gaps identified by that audit without adding gameplay. The repository now
contains executable, persisted, server-authoritative foundations for the specification rather than
documentation-only placeholders.

Implemented after the baseline audit:

- rich block prototypes with independent render, collision, selection, occupancy and occlusion
  data; cube, cross-plane and external mesh models; out-of-cell bounds; prototype-defined halo and
  invalidation radii; cross-chunk occlusion; rich mesh instances; leaf and ore proving content;
- exact `i64` anchored transforms with normalized local offsets, camera-relative rendering and
  local physics-island conversion, including far-world tests above the exact range of `double`;
- stable persisted voxel palette manifests, recoverable missing-prototype objects with opaque saved
  blobs, bounded corrupt-save decoding, and atomic `8 x 8 x 8` cubic region files;
- an authoritative server lifecycle that validates content, owns UUID sessions, loads/saves player
  profiles and discovery, sends profile/map state on join, flushes dirty profiles, writes chat before
  broadcast, and logs joins, leaves and command audits with size/day rotation, lossless compressed
  archives, an append-only rotated index and current-plus-archive queries;
- one `u64 world_time`, lazy selective process evaluation triggers, sleep-based time advancement,
  deterministic interruption policy, and a lazy fuel-buffer fire model with exposure, ember,
  light, warmth, repel and cook-slot data;
- server-owned workpiece blob/flaw/reveal masks, client-local planning separation, per-cell pattern
  and decoration state, deterministic 2D/3D pattern rotations and mirroring, authoritative finish
  validation, structural output metadata, byproduct hooks, versioned persistence and typed
  replication that redacts unrevealed flaws;
- green/dry/fired clay records, data-defined firing outcome tables, pit-firing state, molten draw
  and pour windows, range-validated mould targets, mould careers/cooling data and wear-band copper
  recycling foundations;
- staged assembly ghost blueprints, spatial part layout metadata, named roles and ports,
  construction stages, capabilities, process slots, controlled state transitions for drying and
  maiden firing, persisted state/custom data and authoritative construction commands;
- controlled resource/shader-pack extension declarations and policy checks that forbid gameplay
  paths and raw Vulkan payloads, deterministic asset override priorities, new block-model/pattern/
  fire prototype kinds and richer base-mod data;
- deterministic cave/deposit generation and typed external worldgen feature output for rich blocks,
  block entities, surface objects, large static objects and resource sites;
- richer rooms and route-level logistics effects, an anchored physical-resource world database
  with persistent/replicated typed entity state and runtime-local physics-body reconstruction,
  a complete mandatory debug-overlay registry, an admin service for bans/kicks/roles/profile
  import-export/map reset/backups, plus block-model and cubic-chunk inspection tools;
- explicit chunk vertex layouts in the renderer/Vulkan pipeline and exact world-anchor plus
  camera-relative draw bindings.

Verification at closure: the GCC warning-as-error build succeeds and all 42 registered unit,
foundation and smoke tests pass. Focused v0.2 executables cover coordinates, integrity,
replication, profiles/logs, rich blocks, cubic regions, missing-prototype recovery, the
authoritative server, lazy processes/fire, workpieces, assemblies and cross-cutting infrastructure.

## Engine-readiness hardening follow-up — 2026-07-12

A second implementation pass closed the remaining correctness risks exposed by this report before
gameplay work begins:

- mutating server commands now execute against a staged world and restore external ID allocators on
  every failure path; chunk mutations prevalidate fallible work without invalidating stable chunk
  references;
- voxel commands now carry stable `namespace:prototype` intent while the server derives runtime
  palette cells and light; asset and cooked-asset identities retain their VFS namespace, and
  resource packs explicitly name the namespace they override;
- process, simulation-LOD and replication-interest time is one lossless `u64` world-tick domain,
  with checked large-delta arithmetic and no signed narrowing;
- assembly records persist and validate root/relative part/port coordinates, state consistency and
  spatial layout; workpiece measurements, hidden-mask padding, casting, fire and region mutations
  now validate before commit;
- profile/map pairs commit as one directory generation, live admin edits update connected profiles,
  backups cannot recurse into the world, and ban/profile mutations roll back on persistence errors;
- save, profile, workpiece, command, transport-fragment, log, resource-pack and cooked-manifest
  readers enforce byte/count/allocation budgets; corrupt trailing data and unknown/duplicate fields
  fail closed;
- Vulkan capability reporting now describes active features instead of requested ones, and block
  bounds/halo declarations are validated against their actual geometric reach.

The general bug pass additionally fixed stale-reference invalidation, resource-pack override
classification, profile and assembly revision/state overflow edges, aggregate fragmented-packet and
log-query memory growth, transactional region upserts, and restart-safe log archive naming.

Verification for this follow-up: all 42 registered targets pass under the GCC warnings-as-errors
preset and under Clang AddressSanitizer/UndefinedBehaviorSanitizer. No gameplay feature was added.

This is an engine architecture closure, not a claim that production content or gameplay is done.
The Jolt integration, remote authentication/transport hardening, high-performance greedy meshing,
full lighting/fluid simulation, production shader compilation, GPU presentation polish and game
rules remain later backend/content work. Those do not require violating or replacing the v0.2
storage, authority, time, modding or spatial contracts implemented here.

---

## Baseline audit (before implementation)

Audit date: 2026-07-09  
Scope: engine, game-runtime boundary, samples, tools, tests, base mod, resource packs, and
architecture documents  
Reference contract: [engine_spec.md](engine_spec.md)

## Executive verdict

The repository is a broad and unusually well-tested **engine foundation**, but it is not yet the
v0.2 proving slice. It already has good separation between chunks, build pieces, assemblies,
workpieces, entities, cargo, rooms, networks, processes, save records, networking, and mod data.
It also has real CMake portability guardrails and a useful set of headless sandboxes.

The strongest implemented areas are core/platform abstractions, 32-cubed chunk storage, generic
prototype loading, typed save snapshots, command dispatch, basic workpiece/process/assembly data,
and test infrastructure. The largest architectural gaps are rich blocks and cross-chunk meshing,
stable saved voxel palettes and 3D regions, an actual camera-relative renderer path, authoritative
join/profile/log integration, lazy process evaluation from the one world clock, spatial assembly
layouts, and the v0.2 prototype/resource/shader extension surface.

This audit also found correctness defects that ordinary happy-path tests had not exposed. The
high-confidence fixes made during the audit are listed below. Remaining gaps are deliberately
recorded rather than hidden behind broad “implemented” claims.

Status terms:

- **Implemented**: the required engine concept exists and is meaningfully exercised.
- **Foundation**: useful types/codecs/boundaries exist, but required integration or behavior is
  incomplete.
- **Missing**: no meaningful implementation of the required capability exists yet.
- **Incorrect before audit**: code existed but violated a v0.2 invariant; the listed defect was
  fixed during this audit.

## Corrections completed during this audit

| Area | Finding | Correction |
|---|---|---|
| Coordinates | Dirty regions, simulation subjects, and network derivation narrowed/clamped chunk identity to 32 bits. | Added canonical checked coordinate conversion in `engine/world/coords/world_coords.hpp` and converted derived chunk identities to signed 64-bit. |
| Coordinate arithmetic | Negative division and boundary expansion could produce the wrong chunk or overflow at `i64` limits. | Added floor-division conversions and checked chunk/local-to-block conversion; made dirty expansion and simulation distance overflow-safe. |
| Worldgen height | Terrain height was signed 32-bit and clamped far vertical coordinates. | Widened configured/derived surface height to signed 64-bit, saturated bounded variation, and reject chunks whose cells cannot map to `BlockCoord`. |
| Chunk loading | A failed saved-delta replay could leave a partially loaded chunk in memory. | Made `ChunkStreamer::load_chunk` transactional. |
| Chunk edits | Local coordinates in saved edits/batches were not rejected before mutation. | Validate every local coordinate against `0..31` before committing. |
| Chunk history | Reloading a chunk appended its canonical edit history again. | Replace in-memory history from the canonical saved record on load. |
| Chunk interest | Radius comparisons/enumeration used unchecked signed `center +/- radius`, and an unbounded requested radius could explode planning work. | Use overflow-safe axis distance/saturated bounds and reject load radii beyond the explicit planning budget. |
| Authoritative edits | A command could edit an unloaded coordinate and thereby create an all-air chunk instead of a generated baseline. | Authoritative voxel commands now require the target chunk to be loaded. |
| Save compatibility | A changed prototype fingerprint or extra active mod could still be treated as compatible even though chunks persist numeric voxel types. | Treat both cases as compatibility errors until saved palette mapping exists. This fails safely instead of silently reinterpreting cells. |
| Process transactions | `advance_all` could update earlier records and then report failure on a later record. | Stage the full advancement and commit only after all records resolve successfully. |
| Process handles | The first transactional implementation swapped the entire process map and invalidated previously returned record pointers. | Commit staged values into stable map nodes. |
| Replication sequence | Replication reused each client's command sequence, so two clients issuing command `1` collided in one server stream. | Added a server-owned monotonic replication stream sequence and retained original client/command identity separately. |
| Replication privacy | Relevance was used as a batch-send decision, but unrelated private events, IDs, and typed records could ride inside a sent batch. | Filter actual events, reserved IDs, and typed state snapshots for each recipient; unsafe partial filters request resynchronization. |
| Inventory replication | A transfer only materialized the source inventory. | Replication now carries source and destination inventory owners. |
| World time | Saves and world state had no persisted v0.2 clock; the first audit implementation then accidentally kept metadata and clock as two writable sources. | Added checked `WorldClock` operations over the single metadata-owned `u64 world_time`, removed mutable metadata access, and added text/binary persistence, migration behavior, inspection, and synchronization regressions. |
| Profiles/discovery | No server-side player profile or portable discovery storage existed. | Added UUID-keyed profiles, layered signed-region discovery bitsets, revisioned updates, codecs, bounded reads, serialized same-store creation, atomic per-file replacement, and fail-closed loading when the paired discovery file is absent. |
| Discovery integrity | A received region with an equal/newer revision could replace the mask and clear explored cells. | Require identical masks at equal revisions and reject any newer mask that clears previously discovered bits. Reset must become a separate privileged operation. |
| Admin logs | No server-authoritative join/leave/chat log model existed. | Added typed dual-timestamp log entries, escaped line codec, append/flush, size rotation, current-log queries, and convenience join/leave/chat writers. |
| Log viewer safety | Control bytes in player chat could reach an administrator's terminal. | Percent-encode ASCII control bytes on disk/output and reject unknown viewer categories. |
| Core logging | The global logging mutex remained held while calling a custom sink, so a sink that logged recursively deadlocked. | Copy the sink under the configuration lock and invoke it outside that lock. |
| Architecture/tests/tools | The v0.2 contract and the new foundations were not documented or independently regression-tested. | Added the normative spec, this audit, four focused test executables, a log viewer, and a map-profile inspector. |

## Capability matrix

### 1. Core, platform, and build — Implemented

The project is C++23 with CMake/Ninja presets, warning-as-error builds, GCC/Clang coverage,
ASan/UBSan and TSan presets, stable ID types, diagnostics, logging, assertions, math, job-system
boundaries, virtual filesystem, and headless/native platform abstractions. The tests are registered
through CTest rather than being an undocumented one-off executable.

Remaining work is mainly production hardening: allocator/memory telemetry is light, crash recovery
is not connected to server audit logs, and some backend names imply more capability than their
current smoke implementations provide.

### 2. Coordinates, chunks, and streaming — Foundation

Implemented:

- canonical signed `i64` `BlockCoord` and `ChunkCoord`;
- `32 x 32 x 32` dense chunks;
- negative-safe block/chunk/local conversion and large-coordinate tests;
- dirty/save/mesh/collision-style revisions and edit history;
- deterministic generation and database-backed delta replay;
- authoritative rejection of edits to unloaded chunks.

Missing or incomplete:

- `8 x 8 x 8` 3D region-file storage;
- generation-base checksums and a persisted runtime-palette mapping;
- streaming policy for a truly cubic interest volume;
- integer anchors plus bounded local offsets for all placed/entity/cargo transforms;
- importing a delta without a known/generated base can still conceptually apply it to air outside
  the guarded live command path.

### 3. Rich blocks and block models — Missing

The current voxel is essentially a compact numeric type plus light, and the mesher emits cube
faces. The prototype registry recognizes `voxel`, but not the v0.2 block-model/state families.
There is no separate render/collision/selection/occlusion model, sparse block metadata handle,
out-of-cell render bounds, connected texture model, state model, prototype-declared neighbor
dependency, or mesh invalidation radius.

The current mesher treats every coordinate beyond the owning chunk as air. Consequently adjacent
solid chunks can emit duplicate boundary faces, and no neighbor halo can support leaves, roots,
crystals, or ore protrusions. This is the largest blocker to the requested first visual slice.

### 4. Multi-representation world — Foundation

Separate stores already exist for chunks, build pieces, persistent entities, cargo, inventories,
workpieces, assemblies, processes, rooms, and spatial networks. This is the right architecture and
should be preserved.

Build-piece/entity/cargo transforms are still raw doubles. Beyond about `2^53` cells they cannot
preserve single-cell identity, so they must become an integer world anchor plus local transform.
Physical large resources have a compound-body lifecycle foundation, but are not yet fully included
in world snapshots, save ownership, or typed replication. The resource-to-cargo conversion path
also does not yet preserve an authoritative world origin in every call path.

### 5. Workpieces and patterns — Foundation

The engine has bounded 3D local grids, cell operations and history, template matching, codecs,
prototype materialization, save records, a sandbox, and authoritative cell-edit commands. A 2D
workpiece can be represented as a one-layer grid.

Missing are server-generated hidden flaw/blob masks, planning masks, owner/session state,
per-cell pattern IDs, smoothing/decoration flags, measured output metadata, finish validation,
byproduct hooks, mesh regeneration, typed replication, and the shared mod-defined `PatternLibrary`
with rotations/mirroring and 2D/3D negative shapes.

### 6. World time and lazy processes — Foundation

`world_time` is now a checked, monotonic `u64` stored in world state and both save codecs. The
existing process runtime stores start/update/work/state and can advance deterministically from an
elapsed interval with environment-derived modifiers.

It is not yet the v0.2 lazy model. Commands still use a separate signed millisecond timestamp and
an explicit `process.advance_all` command. Processes are not evaluated selectively on load,
interaction, proximity, inspection, and relevant state changes; weather/season/location streams
are absent; sleep is not wired to world-time advancement; and process persistence does not yet
use a unified world-tick representation. These should be unified before adding fire/crops/machines.

### 7. Fire, clay, firing, and casting — Missing engine specializations

The generic process/workpiece/assembly foundations are appropriate reusable pieces, but there is
no fuel-buffer fire state, ember window, weather exposure, dynamic light/warmth/repel source,
green/dry/fired metadata handoff, firing outcome tables, pit-firing setup, molten/pour-window state,
mould cooling/careers, or wear-band recycling. Implement these as data-driven capabilities after
the time/process and assembly contracts are corrected; do not introduce gameplay tick systems.

### 8. Assemblies and multiblocks — Foundation

Assembly prototypes, required part roles, named ports, record validation, persistence, and an
authoritative create command exist. Base data includes a clay-kiln prototype.

Current parts are references/roles, not validated spatial layouts. There is no root `BlockCoord`,
relative part transform, optional part role, data-defined spatial rule, construction ghost,
staged construction, state machine, process-slot ownership, layout revalidation on world edits,
or mod UI-panel binding. The kiln is therefore only a record-validation sample, not the requested
multiblock proving slice.

### 9. Resources, models, materials, and shader packs — Foundation

The engine has a namespaced VFS, resource-pack manifests/load planning, an asset catalog/cooker,
material prototypes and validation, glTF/GLB metadata handling, texture/audio/font handling,
SPIR-V shader-module plumbing, descriptor/pipeline abstractions, and resource smoke tools.

Important gaps:

- asset logical IDs are currently derived from relative path alone, discarding the VFS namespace;
- equal-priority catalog records can replace an active record based on insertion order;
- resource packs are discovered/ordered but there is no user/server compatibility selection model;
- no block-model prototype or texture-array/connected/seasonal/biome-tint pipeline exists;
- “production shader compiler” accepts SPIR-V rather than compiling the advertised source stack;
- shader-pack extension points and safety validation do not exist;
- the base terrain shader is still template-level content.

### 10. World generation — Foundation

Generation is deterministic from seed/config/region descriptors and produces voxel chunks. Region
graphs are separate from streamed chunks, which is a useful high-level boundary.

It currently generates a simple terrain surface and only a narrow prototype set. There are no
biome/sub-biome volumes, caves/deep deposits, cross-chunk feature ownership, water bodies, rich
features, block entities, ruins, resource sites, future/mastery layers, or hybrid large trees.
Surface height is now signed 64-bit, and generation rejects chunk coordinates whose 32-cell extent
cannot be represented by `BlockCoord` rather than aliasing multiple impossible chunks at `i64`
limits.

### 11. Saves and migrations — Foundation

Implemented are schema/versioned text and binary snapshots, generation-based file save slots,
chunk edit records, major world-store sections, active mod versions/fingerprints, compatibility
checks, ordered migrations, atomic generation commits, and inspection tools. `world_time` is now
persisted and old schema/version data defaults it to zero.

Missing are cubic region files, persisted voxel-palette identity, world config, sparse block
metadata, player-profile references, log indices, missing-prototype placeholders with opaque
round-trip blobs, and migrations for all prototype kinds. Missing prototype references generally
fail validation rather than loading a recoverable placeholder. Binary decoders also trust several
file-provided counts before `reserve`; hard byte/count ceilings are needed to prevent corrupt saves
from forcing excessive allocation.

### 12. Player profiles and map discovery — Foundation

Added during this audit:

- canonical UUID parsing and UUID-keyed server paths;
- display-name history, roles, spawn/bed, waypoints/markers, handbook/progression/settings and
  opaque character data;
- layered `64 x 64` discovery bitsets at signed map-region coordinates;
- negative-safe cell mapping, revisions, deterministic codecs, atomic replacement, list/load/save;
- a command-line map-profile inspector.

Still required are authenticated join/leave integration, incremental network sync, periodic dirty
flush, export/import/reset admin commands, permission ownership, map sampling/exploration policy,
server-shared annotations, and a transaction boundary that commits `profile.dat` and
`map_discovery.dat` as one generation rather than two independently replaced files.

### 13. Server admin logs — Foundation

Added during this audit are typed categories, UTC and world timestamps, calendar/session/event/
player/channel/message/metadata fields, robust escaping, append-and-flush behavior, size rotation,
current-file filters, join/leave/chat helpers, and a log-viewer tool.

The server runtime does not yet call these writers as part of connection/chat/command flows. Daily
rotation, zstd compression, rotated-log indices/query, backup inclusion, moderation/permission/save/
crash audit hooks, address hashing, retention policy, and “write chat before broadcast” transaction
ordering remain open.

### 14. Networking and server authority — Foundation

The engine has authoritative command descriptors, reliable command sequencing, fragmentation,
ack/retry/drop handling, client protocol state, host sessions, interest derivation, replication
events, typed world deltas, application into separate stores, and transport codecs. The audit fixed
multi-client replication sequence collisions and per-recipient payload leakage.

Missing are authentication, permissions/roles, reach and ownership checks, prototype-stable voxel
placement intents, complete chunk snapshot/delta and workpiece/process/profile/discovery replication,
world-config/time join messages, mod/resource/shader compatibility negotiation, profile lifecycle,
chat/admin command pipelines, and resync implementation. The POSIX UDP backend is currently a
packet-host foundation, not a production remote-player accept/authentication flow.

The raw voxel command still accepts a client-provided numeric palette type and light byte. It must
be redesigned around a stable block/prototype intent with server-derived state/light and permission,
reach, replaceability, and inventory validation.

### 15. Modding and vanilla-as-mod — Foundation

`mods/base` is loaded through manifests, dependencies, generic prototypes, patch/final-fix stages,
semantic validators, fingerprints, assets, migrations, and staged client/server script descriptors.
A prototype inspector and mod validator exist.

Only a limited prototype-kind set is recognized: item, cargo, entity, voxel, build piece, assembly,
workpiece, process, room descriptor, material, and scenario. v0.2 block models/states, patterns,
recipes, biomes/features, fire, crops/animals, map layers, shader materials, UI panels, networks/
wards, and admin commands are absent. Field-level creator/patch/stage provenance and conflict output
are incomplete. The TOML-like data reader is deliberately small and cannot represent the full
nested/declarative schemas required here. The restricted Luau backend is a bounded evaluator
foundation, not a complete Luau VM.

### 16. Rooms, logistics, cargo, and physics — Foundation

Rooms have flood extraction, metrics, readable descriptors, dirty scheduling, terrain/build-piece
inputs, and network-access flags. Spatial networks, inventory stacks, cargo properties, transport
tags, physics bodies, compound resources, sleeping, queries, and simple contacts also exist.

Room extraction is currently a bounded origin-relative voxel volume and does not produce persistent
64-bit world volumes or integrate portals, assemblies, light, heat, smoke, weather, warding, or
incremental cross-chunk rebuilds. Road/logistics semantics, vehicle cargo, hazard containers,
settlement pathing, and full physical-resource save/replication are absent. The Jolt backend remains
a placeholder; the deterministic backend is suitable only for foundation tests.

### 17. Renderer and debug overlays — Foundation, not a visual slice

The renderer has an RHI boundary, headless backend, Vulkan instance/device/swapchain/offscreen
clear path, buffer/image upload, shader modules, descriptor/pipeline setup, draw submission, and
structured debug inspection for many non-renderer systems.

It does **not** yet prove terrain rendering. The Vulkan graphics pipeline declares no vertex
attributes, the smoke shader derives a constant/sample position rather than consuming the uploaded
chunk vertex format, mesh draws go through an offscreen path while the surface path mainly clears,
and there is no camera-relative per-draw integer-anchor transform. Material settings are only
partially consumed. Capability reporting can claim validation support based on requested settings
without establishing that the validation layer/debug messenger is active.

None of the mandatory visual overlays—chunk bounds/coords, block render/collision bounds, rich
invalidation, workpiece grids, assembly ports, room volumes, map masks, network interest, or log
tail—has a production draw path yet. Existing structured inspection data is a good source for
building them, but it is not itself an overlay.

### 18. Admin and inspection tools — Foundation

Existing tools cover assets, mod validation, prototypes, replays, saves, shaders, workpieces, and
world snapshots. This audit adds current-log query and player-map inspection. Missing are a server
console, permission/ban/profile commands, rotated-log index/search, block-model viewer, live chunk
inspector, map reset/export/import, teleport/locate, and the mandatory in-render debug overlays.

## Highest-risk remaining correctness work

1. **Persist palette identity before allowing prototype changes.** Current numeric voxel cells are
   safe only because compatibility now fails closed on hash/mod changes.
2. **Make world mutation genuinely transactional.** `WorldOperation` records descriptions and a
   rollback state, but it does not undo already-mutated stores when a later operation stage fails.
3. **Bound every untrusted decoder allocation.** Save, network, profile, log, and mod readers need
   explicit maximum byte/count/depth budgets, not only syntactic validation.
4. **Replace global double positions.** Build pieces, entities, cargo, and physics need signed
   integer anchors plus local floating offsets before far-world support is credible.
5. **Redesign voxel intents.** Clients must not choose runtime palette indices or authoritative
   light and must be subject to permissions, reach, inventory, and placement rules.
6. **Connect generation and persistence transactionally.** A saved delta must never be replayed
   without the exact generated base/generation stamp/palette it describes.
7. **Turn renderer capability flags into proven capabilities.** Validation, vertex layouts,
   descriptors, camera origins, surface draws, and rich block bounds need executable tests.
8. **Integrate profiles/logs with the authoritative server lifecycle.** The new data models are not
   valuable until join, leave, chat, permissions, discovery, backup, and crash paths own them.

## Recommended engine-only implementation order

1. **Rich-block proving slice:** add stable block prototype/state/model definitions, independent
   render/collision/selection/occlusion bounds, neighbor halos, invalidation radii, one cube, one
   ore protrusion, and one out-of-cell leaf block.
2. **Far-world rendering contract:** integer anchors/local offsets, camera-relative transforms,
   actual vertex input, surface mesh draws, validation layer/debug messenger, and bounds overlays.
3. **Stable world persistence:** palette table, generation checksum, `8^3` region container,
   bounded decoders, missing-prototype opaque placeholders, and transactional delta/base loading.
4. **Authoritative server lifecycle:** authentication/session identity, role checks, compatibility
   negotiation, profile/discovery load-sync-save, and join/chat/leave/audit log ordering.
5. **One-clock lazy processes:** replace `server_time_ms` process authority with `world_time`, add
   interval condition replay/evaluation triggers, sleep skip, save/load validation, then fire.
6. **Workpiece/pattern slice:** shared patterns, hidden masks, planning, edit/finish validation,
   output metadata, mesh regeneration, persistence, and filtered replication.
7. **Spatial assembly slice:** integer anchor, relative parts/ports, ghost/stages, validation on edit,
   state machine/process slots, mortar drying, and maiden firing.
8. **Derived settlement fields:** cross-chunk rooms, roads/logistics, physical resources/cargo,
   then power/wards using the same dirty/rebuild/debug infrastructure.

This order keeps gameplay content out of the engine while proving the hard invariants that later
gameplay systems depend on.

## Verification record

Before modification, all six configured build variants compiled and their 26 registered tests
passed: default, warnings-as-errors, Clang, GCC, ASan/UBSan, and TSan (156 test executions).
That broad green baseline did not catch the coordinate narrowing, partial chunk-load mutation,
history duplication, process partial commit, replication sequence/privacy, or logging reentrancy
defects; the focused regression executables added here cover those classes directly.

The final warnings-as-errors build registers 32 CTest targets, including:

- `heartstead_coordinate_v02_tests`;
- `heartstead_world_integrity_v02_tests`;
- `heartstead_replication_v02_tests`;
- `heartstead_server_foundations_v02_tests`.

Final verification passed all 32 targets under the GCC warnings-as-errors preset, all 32 under
Clang AddressSanitizer/UndefinedBehaviorSanitizer, and all 32 under Clang ThreadSanitizer. The 26
smoke targets include the new log-viewer and map-profile-inspector command-line boundaries.
