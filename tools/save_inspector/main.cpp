#include "engine/core/file_io.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/modding/mod_validation.hpp"
#include "engine/save/save_compatibility.hpp"
#include "engine/save/save_slot.hpp"
#include "engine/save/save_text_codec.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void log_diagnostic(const heartstead::modding::ModDiagnostic& diagnostic) {
    const auto severity = heartstead::modding::diagnostic_severity_name(diagnostic.severity);
    const auto level = diagnostic.severity == heartstead::modding::DiagnosticSeverity::error
                           ? heartstead::core::LogLevel::error
                       : diagnostic.severity == heartstead::modding::DiagnosticSeverity::warning
                           ? heartstead::core::LogLevel::warning
                           : heartstead::core::LogLevel::info;

    heartstead::core::log(level, std::string(severity) + ':' + diagnostic.code + ": " +
                                     diagnostic.message + " (" +
                                     diagnostic.source.generic_string() + ")");
}

heartstead::debug::InspectionSeverity
inspection_severity(heartstead::save::SaveCompatibilitySeverity severity) {
    return severity == heartstead::save::SaveCompatibilitySeverity::error
               ? heartstead::debug::InspectionSeverity::error
               : heartstead::debug::InspectionSeverity::warning;
}

void print_usage(const char* executable, std::ostream& output) {
    output << "usage: " << executable << " <save_snapshot.txt> [source_root]\n"
           << "       " << executable << " --slots <save_slots_root>\n";
}

int inspect_slots(const std::filesystem::path& slots_root) {
    const heartstead::save::FileSaveSlotCatalog catalog(slots_root);
    auto summary = catalog.summary();
    if (!summary) {
        heartstead::core::log(heartstead::core::LogLevel::error, summary.error().message);
        return 1;
    }

    auto catalog_inspection = heartstead::debug::Inspector::inspect(summary.value());
    auto has_errors = catalog_inspection.has_errors();
    std::cout << heartstead::debug::Inspector::render_text(catalog_inspection);
    if (!summary.value().slots.empty()) {
        std::cout << '\n';
    }

    for (std::size_t index = 0; index < summary.value().slots.size(); ++index) {
        auto inspection = heartstead::debug::Inspector::inspect(summary.value().slots[index]);
        has_errors = has_errors || inspection.has_errors();
        std::cout << heartstead::debug::Inspector::render_text(inspection);
        if (index + 1 < summary.value().slots.size()) {
            std::cout << '\n';
        }
    }
    return has_errors ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_process_entry(argv[0], [argc, argv] {
        using namespace heartstead;

        if (argc == 2 &&
            (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
            print_usage(argv[0], std::cout);
            return 0;
        }
        if (argc < 2 || argc > 3) {
            print_usage(argv[0], std::cerr);
            return 2;
        }
        if (std::string_view(argv[1]) == "--slots") {
            if (argc != 3) {
                print_usage(argv[0], std::cerr);
                return 2;
            }
            return inspect_slots(argv[2]);
        }

        const std::filesystem::path save_path = argv[1];
        const std::filesystem::path source_root =
            argc >= 3 ? std::filesystem::path(argv[2])
                      : std::filesystem::path(HEARTSTEAD_SOURCE_ROOT);

        auto text = core::read_text_file(save_path);
        if (!text) {
            core::log(core::LogLevel::error, text.error().message);
            return 1;
        }

        auto snapshot = save::SaveTextCodec::decode_snapshot(text.value());
        if (!snapshot) {
            core::log(core::LogLevel::error, snapshot.error().message);
            return 1;
        }

        auto mod_report = modding::ModValidation::validate(source_root / "mods");
        for (const auto& diagnostic : mod_report.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (mod_report.has_errors()) {
            return 1;
        }

        auto inspection = debug::Inspector::inspect(snapshot.value(), &mod_report.registry);
        const auto compatibility = save::SaveCompatibilityChecker::compare(
            snapshot.value().metadata, mod_report.mod_fingerprints);
        inspection.fields.push_back(
            {"compatible_matched_mods", std::to_string(compatibility.matched_mod_count)});
        inspection.fields.push_back(
            {"compatible_missing_mods", std::to_string(compatibility.missing_mod_count)});
        inspection.fields.push_back(
            {"compatible_extra_active_mods", std::to_string(compatibility.extra_active_mod_count)});
        inspection.fields.push_back({"compatible_version_mismatches",
                                     std::to_string(compatibility.version_mismatch_count)});
        inspection.fields.push_back({"compatible_hash_mismatches",
                                     std::to_string(compatibility.prototype_hash_mismatch_count)});
        for (const auto& issue : compatibility.issues) {
            inspection.issues.push_back(debug::InspectionIssue{
                inspection_severity(issue.severity),
                issue.code,
                issue.message,
            });
        }

        std::cout << debug::Inspector::render_text(inspection);
        return inspection.has_errors() ? 1 : 0;
    });
}
