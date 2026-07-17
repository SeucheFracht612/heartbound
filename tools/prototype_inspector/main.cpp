#include "engine/core/ids.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/modding/mod_validation.hpp"
#include "engine/modding/prototype_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

void log_kind_summary(const heartstead::modding::ModValidationReport& report,
                      std::string_view kind) {
    heartstead::core::log(heartstead::core::LogLevel::info,
                          std::string(kind) + '=' + std::to_string(report.count_kind(kind)));
}

void inspect_prototype(const heartstead::modding::GenericPrototype& prototype) {
    heartstead::core::log(heartstead::core::LogLevel::info, prototype.id.value() + " [" +
                                                                prototype.kind + "] " +
                                                                prototype.display_name);
    heartstead::core::log(heartstead::core::LogLevel::info,
                          "source=" + prototype.source.generic_string());

    std::vector<std::string> keys;
    keys.reserve(prototype.fields.size());
    for (const auto& field : prototype.fields) {
        keys.push_back(field.first);
    }
    std::ranges::sort(keys);

    for (const auto& key : keys) {
        const auto found = prototype.fields.find(key);
        if (found != prototype.fields.end()) {
            heartstead::core::log(heartstead::core::LogLevel::info,
                                  "field." + key + '=' + found->second);
        }
    }
}

void print_usage(const char* executable, std::ostream& output) {
    output << "usage: " << executable << " [source_root] [prototype_id]\n"
           << "       " << executable << " [prototype_id]\n";
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
        if (argc > 3) {
            print_usage(argv[0], std::cerr);
            return 2;
        }

        std::filesystem::path source_root = HEARTSTEAD_SOURCE_ROOT;
        std::string_view prototype_id_text;
        if (argc == 2) {
            const std::string_view first_arg(argv[1]);
            if (core::PrototypeId::parse(first_arg).has_value()) {
                prototype_id_text = first_arg;
            } else {
                source_root = std::filesystem::path(argv[1]);
            }
        } else if (argc == 3) {
            source_root = std::filesystem::path(argv[1]);
            prototype_id_text = std::string_view(argv[2]);
        }

        auto report = modding::ModValidation::validate(source_root / "mods");
        for (const auto& diagnostic : report.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (report.has_errors()) {
            return 1;
        }

        if (!prototype_id_text.empty()) {
            const auto prototype_id = core::PrototypeId::parse(prototype_id_text);
            if (!prototype_id) {
                core::log(core::LogLevel::error,
                          "invalid prototype id: " + std::string(prototype_id_text));
                return 2;
            }

            const auto* prototype = report.registry.find(prototype_id.value());
            if (prototype == nullptr) {
                core::log(core::LogLevel::error,
                          "prototype not found: " + prototype_id.value().value());
                return 1;
            }

            inspect_prototype(*prototype);
            return 0;
        }

        core::log(core::LogLevel::info, "Mods: " + std::to_string(report.mods.size()));
        core::log(core::LogLevel::info, "Prototypes: " + std::to_string(report.prototypes.size()));
        log_kind_summary(report, modding::PrototypeKinds::item);
        log_kind_summary(report, modding::PrototypeKinds::cargo);
        log_kind_summary(report, modding::PrototypeKinds::entity);
        log_kind_summary(report, modding::PrototypeKinds::voxel);
        log_kind_summary(report, modding::PrototypeKinds::block_model);
        log_kind_summary(report, modding::PrototypeKinds::build_piece);
        log_kind_summary(report, modding::PrototypeKinds::assembly);
        log_kind_summary(report, modding::PrototypeKinds::workpiece);
        log_kind_summary(report, modding::PrototypeKinds::pattern);
        log_kind_summary(report, modding::PrototypeKinds::process);
        log_kind_summary(report, modding::PrototypeKinds::fire);
        log_kind_summary(report, modding::PrototypeKinds::room_descriptor);
        log_kind_summary(report, modding::PrototypeKinds::material);
        log_kind_summary(report, modding::PrototypeKinds::scenario);

        std::vector<const modding::GenericPrototype*> prototypes;
        prototypes.reserve(report.prototypes.size());
        for (const auto& prototype : report.prototypes) {
            prototypes.push_back(&prototype);
        }
        std::ranges::sort(prototypes, {}, [](const modding::GenericPrototype* prototype) {
            return prototype->id.value();
        });
        for (const auto* prototype : prototypes) {
            core::log(core::LogLevel::info, prototype->id.value() + " [" + prototype->kind + "]");
        }

        return 0;
    });
}
