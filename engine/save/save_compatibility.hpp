#pragma once

#include "engine/modding/mod_fingerprint.hpp"
#include "engine/save/save_metadata.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::save {

enum class SaveCompatibilitySeverity {
    warning,
    error,
};

struct SaveCompatibilityIssue {
    SaveCompatibilitySeverity severity = SaveCompatibilitySeverity::warning;
    std::string code;
    std::string mod_id;
    std::string message;
};

struct SaveCompatibilityReport {
    std::size_t matched_mod_count = 0;
    std::size_t missing_mod_count = 0;
    std::size_t extra_active_mod_count = 0;
    std::size_t version_mismatch_count = 0;
    std::size_t prototype_hash_mismatch_count = 0;
    std::vector<SaveCompatibilityIssue> issues;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool has_warnings() const noexcept;
};

class SaveCompatibilityChecker {
  public:
    [[nodiscard]] static SaveCompatibilityReport
    compare(const SaveMetadata& saved_metadata,
            const std::vector<modding::ModPrototypeFingerprint>& active_fingerprints);
};

[[nodiscard]] std::string_view
save_compatibility_severity_name(SaveCompatibilitySeverity severity) noexcept;

} // namespace heartstead::save
