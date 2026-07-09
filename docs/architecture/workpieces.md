# Workpiece Architecture

Workpieces are local editable crafting objects. They are not terrain chunks.

Examples:

- stone knapping blank
- clay lump
- clay vessel
- mold
- heated ingot
- wooden carving block
- rune plate

The reusable low-level implementation belongs in `engine/workpieces/`. Heartstead-
specific crafting rules belong in `game/systems/workpieces/`. Vanilla definitions live
under `mods/base/data/workpieces/`.

The first implementation milestone should support:

- stable workpiece ids
- a compact local grid
- basic add/remove cell operations
- operation history suitable for server validation
- template matching
- save/load
- debug inspection output

Implemented foundation:

- `WorkpieceDefinition`
  - materialized from workpiece prototypes
  - stores stable workpiece prototype id, material prototype id, and local grid shape
  - creates empty local `WorkpieceGrid` instances without touching terrain chunks

- `WorkpieceGrid`
  - compact local editable grid
  - add/remove/set cell operations
  - operation history
  - server-authoritative `workpiece.edit_cell` command path through `WorldCommandRegistry`

- `WorkpieceTemplate`
  - required cells
  - forbidden cells
  - strict matching for unexpected occupied cells

- `WorkpieceGridTextCodec`
  - small versioned text format for occupied local cells
  - shape validation on decode
  - save snapshot validation decodes payloads and rejects shape mismatches before import
  - usable by early sandbox and save/debug tools

- `heartstead_workpiece_inspector`
  - reads encoded workpiece grid text
  - reports shape, occupied cells, operation count, and material counts through the
    structured debug inspection layer

This layer remains separate from `VoxelChunk` and `ChunkDatabase`. Workpiece cells are
local crafting state, not terrain data.
