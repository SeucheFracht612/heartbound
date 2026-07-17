#include "engine/save/save_migration.hpp"

#include "engine/core/ids.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::save {

core::Status SaveMigrationStep::validate() const {
    if (!core::is_valid_local_id(id)) {
        return core::Status::failure("save_migration.invalid_id",
                                     "save migration id must be a safe local id");
    }
    if (from_schema_version == 0 || to_schema_version == 0) {
        return core::Status::failure("save_migration.invalid_schema",
                                     "save migration schema versions must be non-zero");
    }
    if (to_schema_version <= from_schema_version) {
        return core::Status::failure("save_migration.invalid_order",
                                     "save migration must move to a newer schema version");
    }
    if (description.empty()) {
        return core::Status::failure("save_migration.missing_description",
                                     "save migration needs a description");
    }
    if (!apply) {
        return core::Status::failure("save_migration.missing_apply",
                                     "save migration needs an apply callback");
    }
    return core::Status::ok();
}

core::Status SaveMigrationRegistry::register_migration(SaveMigrationStep migration) {
    auto status = migration.validate();
    if (!status) {
        return status;
    }
    if (find_by_id(migration.id) != nullptr) {
        return core::Status::failure("save_migration.duplicate_id",
                                     "duplicate save migration id: " + migration.id);
    }
    if (find_from_schema(migration.from_schema_version) != nullptr) {
        return core::Status::failure("save_migration.duplicate_from_schema",
                                     "duplicate save migration source schema: " +
                                         std::to_string(migration.from_schema_version));
    }

    migrations_.push_back(std::move(migration));
    std::ranges::sort(migrations_, {}, &SaveMigrationStep::from_schema_version);
    return core::Status::ok();
}

const SaveMigrationStep* SaveMigrationRegistry::find_by_id(std::string_view id) const noexcept {
    const auto found = std::ranges::find_if(
        migrations_, [id](const SaveMigrationStep& migration) { return migration.id == id; });
    return found == migrations_.end() ? nullptr : &*found;
}

const SaveMigrationStep*
SaveMigrationRegistry::find_from_schema(std::uint32_t schema_version) const noexcept {
    const auto found =
        std::ranges::find_if(migrations_, [schema_version](const SaveMigrationStep& migration) {
            return migration.from_schema_version == schema_version;
        });
    return found == migrations_.end() ? nullptr : &*found;
}

core::Status SaveMigrationRegistry::validate_path(std::uint32_t from_schema_version,
                                                  std::uint32_t target_schema_version) const {
    if (from_schema_version == 0 || target_schema_version == 0) {
        return core::Status::failure("save_migration.invalid_schema",
                                     "save migration path schemas must be non-zero");
    }
    if (from_schema_version > target_schema_version) {
        return core::Status::failure("save_migration.downgrade_unsupported",
                                     "save migration cannot downgrade schema versions");
    }

    auto schema = from_schema_version;
    while (schema < target_schema_version) {
        const auto* migration = find_from_schema(schema);
        if (migration == nullptr) {
            return core::Status::failure("save_migration.missing_path",
                                         "missing save migration from schema " +
                                             std::to_string(schema));
        }
        if (migration->to_schema_version > target_schema_version) {
            return core::Status::failure("save_migration.path_overshoots_target",
                                         "save migration " + migration->id +
                                             " overshoots target schema " +
                                             std::to_string(target_schema_version));
        }
        schema = migration->to_schema_version;
    }

    return core::Status::ok();
}

std::size_t SaveMigrationRegistry::migration_count() const noexcept {
    return migrations_.size();
}

core::Result<SaveMigrationResult>
SaveMigrationRunner::migrate(SaveSnapshot& snapshot, const SaveMigrationRegistry& registry,
                             std::uint32_t target_schema_version) {
    SaveMigrationResult result;
    result.previous_schema_version = snapshot.metadata.schema_version;
    result.final_schema_version = snapshot.metadata.schema_version;

    auto status = snapshot.metadata.validate();
    if (!status) {
        return core::Result<SaveMigrationResult>::failure(status.error().code,
                                                          status.error().message);
    }

    status = registry.validate_path(snapshot.metadata.schema_version, target_schema_version);
    if (!status) {
        return core::Result<SaveMigrationResult>::failure(status.error().code,
                                                          status.error().message);
    }

    SaveSnapshot staged_snapshot = snapshot;
    while (staged_snapshot.metadata.schema_version < target_schema_version) {
        const auto* migration = registry.find_from_schema(staged_snapshot.metadata.schema_version);
        if (migration == nullptr) {
            return core::Result<SaveMigrationResult>::failure(
                "save_migration.missing_path",
                "missing save migration from schema " +
                    std::to_string(staged_snapshot.metadata.schema_version));
        }
        if (has_migration_history_entry(staged_snapshot.metadata, migration->id)) {
            return core::Result<SaveMigrationResult>::failure(
                "save_migration.history_conflict",
                "save migration history already contains pending migration: " + migration->id);
        }

        status = migration->apply(staged_snapshot);
        if (!status) {
            return core::Result<SaveMigrationResult>::failure(status.error().code,
                                                              status.error().message);
        }

        staged_snapshot.metadata.schema_version = migration->to_schema_version;
        staged_snapshot.metadata.migration_history.push_back(migration->id);
        result.applied_migrations.push_back(migration->id);
        result.final_schema_version = migration->to_schema_version;
    }

    snapshot = std::move(staged_snapshot);
    return core::Result<SaveMigrationResult>::success(std::move(result));
}

bool has_migration_history_entry(const SaveMetadata& metadata,
                                 std::string_view migration_id) noexcept {
    return std::ranges::any_of(metadata.migration_history, [migration_id](const std::string& id) {
        return id == migration_id;
    });
}

} // namespace heartstead::save
