#pragma once

#include "engine/core/result.hpp"
#include "engine/save/save_snapshot.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::save {

struct SaveMigrationStep {
    std::string id;
    std::uint32_t from_schema_version = 0;
    std::uint32_t to_schema_version = 0;
    std::string description;
    std::function<core::Status(SaveSnapshot&)> apply;

    [[nodiscard]] core::Status validate() const;
};

struct SaveMigrationResult {
    std::uint32_t previous_schema_version = 0;
    std::uint32_t final_schema_version = 0;
    std::vector<std::string> applied_migrations;
};

class SaveMigrationRegistry {
  public:
    [[nodiscard]] core::Status register_migration(SaveMigrationStep migration);
    [[nodiscard]] const SaveMigrationStep* find_by_id(std::string_view id) const noexcept;
    [[nodiscard]] const SaveMigrationStep*
    find_from_schema(std::uint32_t schema_version) const noexcept;
    [[nodiscard]] core::Status validate_path(std::uint32_t from_schema_version,
                                             std::uint32_t target_schema_version) const;
    [[nodiscard]] std::size_t migration_count() const noexcept;

  private:
    std::vector<SaveMigrationStep> migrations_;
};

class SaveMigrationRunner {
  public:
    [[nodiscard]] static core::Result<SaveMigrationResult>
    migrate(SaveSnapshot& snapshot, const SaveMigrationRegistry& registry,
            std::uint32_t target_schema_version);
};

[[nodiscard]] bool has_migration_history_entry(const SaveMetadata& metadata,
                                               std::string_view migration_id) noexcept;

} // namespace heartstead::save
