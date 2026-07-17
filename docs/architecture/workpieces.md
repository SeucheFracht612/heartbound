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

The reusable low-level implementation belongs in `engine/workpieces/`. Heartstead-specific
crafting rules belong above that engine boundary in gameplay modules. Vanilla definitions live
under `mods/base/data/workpieces/`.

Implemented foundation:

- `WorkpieceDefinition`
  - materialized from workpiece prototypes
  - stores stable workpiece prototype id, material prototype id, and local grid shape
  - creates empty local `WorkpieceGrid` instances without touching terrain chunks

- `WorkpieceRecord`
  - stable workpiece/prototype identity, local grid, material prototype, optional private server
    state, owning session, monotonic revision, and committed flag
  - validates revision and server-state shape/material invariants
  - round-trips rich state through text/binary saves and the world snapshot bridge

- `WorkpieceGrid`
  - compact local editable grid
  - add/remove/set cell operations
  - operation history
  - server-authoritative `workpiece.edit_cell` command path through `WorldCommandRegistry`
  - owner/committed checks, optional server-blob bounds, hidden-flaw reveal, revision advance, save
    dirtiness, and replication event emission on accepted edits

- `WorkpieceTemplate`
  - required cells
  - forbidden cells
  - strict matching for unexpected occupied cells

- `PatternLibrary`
  - materializes `pattern` prototypes with 3D shapes, occupied cells, optional material
    constraints, strict matching, Y rotations, X mirroring, negative-mould classification, and an
    output prototype id
  - enumerates transformed variants deterministically and matches them against a local grid

- `WorkpieceServerState`
  - deterministic private blob and hidden-flaw masks plus revealed cells and derived output metadata
  - validates mask sizes against the grid and reveals a targeted flaw only when the authoritative
    edit reaches that cell
  - has a versioned text codec; replication masks unrevealed hidden flaws before encoding the
    public workpiece record

- `workpiece.finish`
  - optionally verifies a requested pattern id against the server's deterministic match
  - derives an output prototype, classification, mass/shape measurements, and byproduct quantity
  - records output metadata and commits the workpiece so it cannot be edited or finished twice

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
