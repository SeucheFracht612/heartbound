#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/modding/mod_fingerprint.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::save {

inline constexpr std::uint32_t current_save_schema_version = 2;

struct SavedModRecord {
    std::string id;
    std::string version;
    std::string prototype_hash;
};

struct SaveMetadata {
    std::uint32_t schema_version = current_save_schema_version;
    std::string game_version;
    std::uint64_t world_seed = 0;
    std::uint64_t world_time = 0;
    std::vector<SavedModRecord> enabled_mods;
    std::vector<std::string> migration_history;

    [[nodiscard]] core::Status validate() const;
};

[[nodiscard]] std::vector<SavedModRecord> saved_mod_records_from_fingerprints(
    const std::vector<modding::ModPrototypeFingerprint>& fingerprints);

class SaveIdAllocator {
  public:
    explicit SaveIdAllocator(std::uint64_t next_value = 1);

    [[nodiscard]] core::Result<core::SaveId> reserve();
    [[nodiscard]] core::SaveId peek_next() const noexcept;

  private:
    std::uint64_t next_value_ = 1;
};

} // namespace heartstead::save
