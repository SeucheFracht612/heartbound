#include "engine/core/file_io.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/modding/mod_validation.hpp"
#include "engine/save/save_text_codec.hpp"
#include "engine/world/world_snapshot.hpp"
#include "engine/world/world_state.hpp"

#include <filesystem>
#include <iostream>
#include <string>

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

void print_usage(const char* executable, std::ostream& output) {
    output << "usage: " << executable << " [save_snapshot.txt [source_root]]\n";
}

heartstead::world::WorldState make_empty_world() {
    heartstead::world::WorldStateDesc desc;
    desc.metadata.game_version = "world_inspector";
    return heartstead::world::WorldState(desc);
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

        if (argc == 1) {
            auto world = make_empty_world();
            std::cout << debug::Inspector::render_text(debug::Inspector::inspect(world));
            return 0;
        }

        const std::filesystem::path source_root =
            argc >= 3 ? std::filesystem::path(argv[2])
                      : std::filesystem::path(HEARTSTEAD_SOURCE_ROOT);

        auto text = core::read_text_file(argv[1]);
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

        auto imported_world = world::WorldSnapshotBridge::import_validated_snapshot(
            snapshot.value(), mod_report.registry);
        if (!imported_world) {
            core::log(core::LogLevel::error, imported_world.error().message);
            return 1;
        }

        const auto inspection = debug::Inspector::inspect(imported_world.value());
        std::cout << debug::Inspector::render_text(inspection);
        return inspection.has_errors() ? 1 : 0;
    });
}
