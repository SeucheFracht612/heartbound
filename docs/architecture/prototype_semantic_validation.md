# Prototype Semantic Validation

Generic prototype loading checks file shape, stable ids, namespaces, duplicate ids, and
known representation kinds. Data-update and final-fix prototype patches are applied
before semantic validation. Semantic validation then adds engine-owned field checks for
the effective prototype set without taking over gameplay balance.

Token-list fields are validated as unique local-id tokens. Empty, malformed, and
duplicate tokens are reported before runtime materialization.

Current checks:

- `item`: positive `stack_limit`, optional tag list shape
- `cargo`: positive mass and volume, known transport modes, optional hazard tags
- `entity`: known entity kind and optional persistent flag
- `voxel`: terrain material, mining tool, and optional tag tokens
- `build_piece`: material, room contribution, and network port token lists
- `assembly`: required part records and build-piece prototype references
- `workpiece`: grid shape and material prototype references
- `process`: positive default work duration, optional room/power requirements, optional required
  power capacity, optional base quality rate, and optional tags
- `material`: renderer-facing domain, blend mode, shader template, texture, scalar, and color
  fields
- `scenario`: startup region, spawn mode, and starting item/cargo references

Aggregate content validation also builds the voxel palette from validated `voxel`
prototypes. Palette construction reserves terrain cell type `0` for air and assigns
content voxel types from stable prototype ids.

`ModValidation` runs these checks after generic loading and prototype registry build, so
`heartstead_mod_validator` reports semantic content errors through the same diagnostic
pipeline used by the base mod and community mods.
