# Prototype Semantic Validation

Generic prototype loading checks file shape, stable ids, namespaces, duplicate ids, and
known representation kinds. Data-update and final-fix prototype patches are applied
before semantic validation. Semantic validation then adds engine-owned field checks for
the effective prototype set without taking over gameplay balance.

Token-list fields are validated as unique local-id tokens. Empty, malformed, and
duplicate tokens are reported before runtime materialization.

Current checks:

- `item`: positive `stack_limit`, optional non-negative mass, optional tag list shape
- `cargo`: positive mass and volume, known transport modes, optional hazard tags
- `entity`: known entity kind, optional persistent flag, and optional positive carry capacity
- `voxel`: terrain material, mining tool, optional tag tokens, occupancy/occlusion and bounds,
  light/metadata fields, and optional block-model reference
- `block_model`: typed geometry, material/render phase, flags, bounds, and dependency radii through
  the block-model materializer
- `build_piece`: material, room contribution, and network port token lists
- `assembly`: required part records and build-piece prototype references
- `workpiece`: grid shape and material prototype references
- `pattern`: shape/cells/output syntax, rotations/mirroring/strictness, negative-mould flag, and
  material-constraint syntax through the pattern materializer
- `process`: positive canonical `default_required_work_ticks` (with legacy
  `default_required_work_ms` input compatibility), optional room/power requirements, optional
  required power capacity, optional base quality rate, and optional tags
- `fire`: fuel/ember durations, light/cook-slot bounds, and finite non-negative warmth/repel radii
  through the fire materializer
- `room_descriptor`: local descriptor code, known severity, and optional tags
- `material`: renderer-facing domain, blend mode, shader template, texture, scalar, and color
  fields
- `scenario`: startup region, spawn mode, and starting item/cargo references

Aggregate content validation also builds the voxel palette from validated `voxel`
and `block_model` prototypes. Palette construction reserves terrain cell type `0` for air and
assigns content voxel types from stable prototype ids.

Reserved gameplay-owned kinds are intentionally not listed above. They are preserved and
fingerprinted by the generic registry, but their owning gameplay module must supply field-level
validation and materialization before using them.

`ModValidation` runs these checks after generic loading and prototype registry build, so
`heartstead_mod_validator` reports semantic content errors through the same diagnostic
pipeline used by the base mod and community mods.
