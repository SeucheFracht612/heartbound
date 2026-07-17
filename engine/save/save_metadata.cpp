#include "engine/save/save_metadata.hpp"

#include <unordered_set>

namespace heartstead::save {

core::Status SaveMetadata::validate() const {
    if (schema_version == 0) {
        return core::Status::failure("save.invalid_schema", "save schema version must be non-zero");
    }

    if (game_version.empty()) {
        return core::Status::failure("save.missing_game_version",
                                     "save metadata needs a game version");
    }

    std::unordered_set<std::string> seen_mod_ids;
    for (const auto& mod : enabled_mods) {
        if (!core::is_valid_namespace_id(mod.id)) {
            return core::Status::failure("save.invalid_mod_id",
                                         "saved mod id is not a valid namespace id: " + mod.id);
        }
        if (!seen_mod_ids.insert(mod.id).second) {
            return core::Status::failure("save.duplicate_mod",
                                         "saved mod record is duplicated: " + mod.id);
        }
        if (mod.version.empty()) {
            return core::Status::failure("save.missing_mod_version",
                                         "saved mod record needs a version: " + mod.id);
        }
        if (mod.prototype_hash.empty()) {
            return core::Status::failure("save.missing_mod_prototype_hash",
                                         "saved mod record needs a prototype hash: " + mod.id);
        }
    }

    std::unordered_set<std::string> seen_migrations;
    for (const auto& migration : migration_history) {
        if (migration.empty()) {
            return core::Status::failure("save.empty_migration_history",
                                         "migration history entries must not be empty");
        }
        if (!core::is_valid_local_id(migration)) {
            return core::Status::failure("save.invalid_migration_history",
                                         "migration history entry must be a safe local id: " +
                                             migration);
        }
        if (!seen_migrations.insert(migration).second) {
            return core::Status::failure("save.duplicate_migration_history",
                                         "migration history entry is duplicated: " + migration);
        }
    }

    return core::Status::ok();
}

std::vector<SavedModRecord> saved_mod_records_from_fingerprints(
    const std::vector<modding::ModPrototypeFingerprint>& fingerprints) {
    std::vector<SavedModRecord> records;
    records.reserve(fingerprints.size());
    for (const auto& fingerprint : fingerprints) {
        records.push_back(SavedModRecord{
            fingerprint.id,
            fingerprint.version,
            fingerprint.prototype_hash,
        });
    }
    return records;
}

SaveIdAllocator::SaveIdAllocator(std::uint64_t next_value)
    : next_value_(next_value == 0 ? 1 : next_value) {}

core::Result<core::SaveId> SaveIdAllocator::reserve() {
    if (next_value_ == 0) {
        return core::Result<core::SaveId>::failure("save.id_exhausted",
                                                   "save id allocator exhausted its id range");
    }

    const auto id = core::SaveId::from_value(next_value_);
    ++next_value_;
    return core::Result<core::SaveId>::success(id);
}

core::SaveId SaveIdAllocator::peek_next() const noexcept {
    return core::SaveId::from_value(next_value_);
}

} // namespace heartstead::save
