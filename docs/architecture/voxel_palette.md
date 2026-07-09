# Voxel Palette

The voxel palette maps mod-defined voxel prototypes to compact terrain cell ids.

Implemented foundation:

- `VoxelDefinition`
  - content voxel type id
  - source prototype id
  - display name
  - terrain material token
  - mining tool token
  - optional tags

- `VoxelPalette`
  - reserves type `0` for air
  - assigns content voxel types starting at `1`
  - looks up definitions by type or prototype id
  - creates `VoxelCell` values from prototype ids

- aggregate content validation
  - builds the palette from loaded `voxel` prototypes
  - exposes the palette to tools and game-runtime startup reports

Chunks intentionally store compact numeric `VoxelCell` values. Mods and gameplay systems
should speak stable prototype ids such as `base:voxels/clay`, then resolve through the
palette at command/worldgen boundaries. This keeps streamed terrain storage small without
turning chunk cells into the only source of voxel meaning.

Type `0` is permanently reserved for air. Content voxel ids are deterministic for a given
loaded prototype set because the palette sorts voxel prototype ids before assigning types.
Future save migration work can record prototype hashes alongside save metadata to detect
when a saved numeric type table needs migration.
