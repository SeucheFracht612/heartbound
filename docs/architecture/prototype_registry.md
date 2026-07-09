# Prototype Registry

The generic prototype loader reads raw mod data. The prototype registry turns those
loaded definitions into an engine-facing lookup table.

Implemented foundation:

- known prototype kinds:
  - item
  - cargo
  - entity
  - voxel
  - build piece
  - assembly
  - workpiece
  - process
  - material
  - scenario
- id lookup
- kind indexes
- staged prototype patches applied before registry build
- required-reference validation
- expected-kind validation
- shared validation reports for tools
- prototype inspection output for source file and raw fields
- semantic checks for engine-owned representation fields

Prototype patch files currently use two stage suffixes:

- `*.prototype_patch.toml` for the data-update stage
- `*.final_patch.toml` for the final-fix stage

The current patch contract is intentionally small:

- `target = "namespace:prototype_id"`
- `set.<field> = "value"`

Patches are applied after all prototype definitions are loaded. Data-update patches run
first, then final-fix patches run, and only then does semantic validation see the
effective prototype set. Patches may update raw fields such as `stack_limit`, `tags`, or
`display_name`; they may not change immutable identity fields such as `id` or `kind`.
Cross-mod patches require a manifest dependency on the target prototype's namespace.

Scenario prototypes are the game-start anchor for a world/session. The engine-level
semantic checks only validate their representation-facing fields and references:

- `start_region`
- `spawn_mode`
- `starting_items`
- `starting_cargo`
- `tags`

Aggregate content validation materializes selected engine-owned prototype outputs:

- item prototypes into `ItemDefinition` records
- cargo prototypes into `CargoDefinition` records
- entity prototypes into `EntityDefinition` records
- voxel prototypes into `VoxelPalette`
- assembly prototypes into `AssemblyDefinition` records
- process prototypes into `ProcessDefinition` records
- workpiece prototypes into `WorkpieceDefinition` records
- material prototypes into renderer material definitions
- scenario prototypes into `ScenarioDefinition` records

The registry deliberately validates representation kind, not gameplay meaning. Game
runtime systems should use it to reject references such as an item id where a cargo id,
build piece id, or scenario id is required.
