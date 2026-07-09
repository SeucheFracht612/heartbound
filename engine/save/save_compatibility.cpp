#include "engine/save/save_compatibility.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace heartstead::save {

namespace {

void add_issue(SaveCompatibilityReport& report, SaveCompatibilitySeverity severity,
               std::string code, std::string mod_id, std::string message) {
    report.issues.push_back(SaveCompatibilityIssue{
        severity,
        std::move(code),
        std::move(mod_id),
        std::move(message),
    });
}

} // namespace

bool SaveCompatibilityReport::has_errors() const noexcept {
    return std::ranges::any_of(issues, [](const SaveCompatibilityIssue& issue) {
        return issue.severity == SaveCompatibilitySeverity::error;
    });
}

bool SaveCompatibilityReport::has_warnings() const noexcept {
    return std::ranges::any_of(issues, [](const SaveCompatibilityIssue& issue) {
        return issue.severity == SaveCompatibilitySeverity::warning;
    });
}

SaveCompatibilityReport SaveCompatibilityChecker::compare(
    const SaveMetadata& saved_metadata,
    const std::vector<modding::ModPrototypeFingerprint>& active_fingerprints) {
    SaveCompatibilityReport report;
    std::unordered_map<std::string, const modding::ModPrototypeFingerprint*> active_by_id;
    std::set<std::string> saved_ids;

    for (const auto& active : active_fingerprints) {
        if (!active_by_id.emplace(active.id, &active).second) {
            add_issue(report, SaveCompatibilitySeverity::error,
                      "save_compatibility.duplicate_active_mod", active.id,
                      "active content contains duplicate mod fingerprint: " + active.id);
        }
    }

    for (const auto& saved_mod : saved_metadata.enabled_mods) {
        if (!saved_ids.insert(saved_mod.id).second) {
            add_issue(report, SaveCompatibilitySeverity::error,
                      "save_compatibility.duplicate_saved_mod", saved_mod.id,
                      "save metadata contains duplicate enabled mod: " + saved_mod.id);
            continue;
        }

        const auto active = active_by_id.find(saved_mod.id);
        if (active == active_by_id.end()) {
            ++report.missing_mod_count;
            add_issue(report, SaveCompatibilitySeverity::error, "save_compatibility.missing_mod",
                      saved_mod.id, "saved mod is not active: " + saved_mod.id);
            continue;
        }

        ++report.matched_mod_count;
        if (active->second->version != saved_mod.version) {
            ++report.version_mismatch_count;
            add_issue(report, SaveCompatibilitySeverity::warning,
                      "save_compatibility.mod_version_mismatch", saved_mod.id,
                      "saved mod version " + saved_mod.version + " differs from active version " +
                          active->second->version + ": " + saved_mod.id);
        }

        if (active->second->prototype_hash != saved_mod.prototype_hash) {
            ++report.prototype_hash_mismatch_count;
            add_issue(report, SaveCompatibilitySeverity::error,
                      "save_compatibility.prototype_hash_mismatch", saved_mod.id,
                      "saved prototype hash " + saved_mod.prototype_hash +
                          " differs from active hash " + active->second->prototype_hash + ": " +
                          saved_mod.id);
        }
    }

    for (const auto& active : active_fingerprints) {
        if (!saved_ids.contains(active.id)) {
            ++report.extra_active_mod_count;
            add_issue(report, SaveCompatibilitySeverity::error,
                      "save_compatibility.extra_active_mod", active.id,
                      "active mod was not enabled in the save metadata and may change compact "
                      "prototype palette assignments: " +
                          active.id);
        }
    }

    return report;
}

std::string_view save_compatibility_severity_name(SaveCompatibilitySeverity severity) noexcept {
    switch (severity) {
    case SaveCompatibilitySeverity::warning:
        return "warning";
    case SaveCompatibilitySeverity::error:
        return "error";
    }
    return "unknown";
}

} // namespace heartstead::save
