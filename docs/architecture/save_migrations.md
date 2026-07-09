# Save Migrations

Save migrations are engine-owned, ordered schema steps. A migration has:

- stable migration id
- source schema version
- target schema version
- description
- apply callback over `SaveSnapshot`

`SaveMigrationRegistry` accepts one migration per source schema version and rejects
duplicate migration ids. `SaveMigrationRunner` walks from the snapshot schema to a
target schema, applies each step, advances `SaveMetadata::schema_version`, and appends
the migration id to `SaveMetadata::migration_history` only after the step succeeds.

The migration layer operates on the in-memory `SaveSnapshot` contract. `FileSaveDatabase` can read
the active snapshot, run the same ordered migration runner to a requested schema, and write the
upgraded snapshot back as a new generation only when migrations actually apply. Future database or
binary save formats should preserve the same ordering and history rules before loading gameplay
systems.

Rules:

- migrations only move forward
- missing migration paths fail explicitly
- already-recorded pending migration ids are treated as history conflicts
- migrations update authoritative saved data only, not derived caches
