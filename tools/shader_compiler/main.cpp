#include "engine/assets/asset_catalog.hpp"
#include "engine/assets/resource_pack.hpp"
#include "engine/core/logging.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/modding/mod_discovery.hpp"
#include "engine/renderer/shaders/shader_compiler.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void log_diagnostic(const heartstead::modding::ModDiagnostic& diagnostic) {
    using heartstead::core::LogLevel;
    using heartstead::modding::DiagnosticSeverity;

    const auto level = diagnostic.severity == DiagnosticSeverity::error     ? LogLevel::error
                       : diagnostic.severity == DiagnosticSeverity::warning ? LogLevel::warning
                                                                            : LogLevel::info;

    heartstead::core::log(level, diagnostic.code + ": " + diagnostic.message + " (" +
                                     diagnostic.source.generic_string() + ")");
}

void print_usage(const char* executable) {
    std::cerr << "usage: " << executable
              << " [source_root] [shader_manifest_path] [development|production] [--inspect]\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace heartstead;

    auto source_root = std::filesystem::path(HEARTSTEAD_SOURCE_ROOT);
    std::filesystem::path output_path;
    std::string profile = "development";
    bool inspect_after_compile = false;
    auto positional_count = 0;
    for (int index = 1; index < argc; ++index) {
        const auto argument = std::string_view(argv[index]);
        if (argument == "--inspect") {
            inspect_after_compile = true;
            continue;
        }
        if (argument.starts_with("--")) {
            print_usage(argv[0]);
            return 2;
        }

        switch (positional_count) {
        case 0:
            source_root = std::filesystem::path(argv[index]);
            break;
        case 1:
            output_path = std::filesystem::path(argv[index]);
            break;
        case 2:
            profile = std::string(argument);
            break;
        default:
            print_usage(argv[0]);
            return 2;
        }
        ++positional_count;
    }
    if (output_path.empty()) {
        output_path = source_root / "build" / "compiled_shaders" / "shader_manifest.txt";
    }

    assets::AssetCatalog catalog;

    modding::ModDiscoverer mod_discoverer;
    auto mods = mod_discoverer.discover(source_root / "mods");
    for (const auto& diagnostic : mods.diagnostics) {
        log_diagnostic(diagnostic);
    }
    if (mods.has_errors()) {
        return 1;
    }

    for (const auto& mod : mods.mods) {
        auto indexed = assets::AssetCatalogBuilder::index_directory(
            catalog, mod.root / "assets", mod.id, assets::AssetSourceKind::mod, mod.id, 0);
        for (const auto& diagnostic : indexed.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (indexed.has_errors()) {
            return 1;
        }
    }

    assets::ResourcePackDiscoverer pack_discoverer;
    auto packs = pack_discoverer.discover(source_root / "resource_packs");
    for (const auto& diagnostic : packs.diagnostics) {
        log_diagnostic(diagnostic);
    }
    if (packs.has_errors()) {
        return 1;
    }

    auto pack_plan = assets::ResourcePackLoadPlanner::plan(packs.packs);
    if (!pack_plan) {
        core::log(core::LogLevel::error, pack_plan.error().message);
        return 1;
    }

    for (const auto& entry : pack_plan.value().entries) {
        auto indexed = assets::AssetCatalogBuilder::index_directory(
            catalog, entry.manifest.root / "assets", entry.manifest.id,
            assets::AssetSourceKind::resource_pack, entry.manifest.id, entry.asset_priority);
        for (const auto& diagnostic : indexed.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (indexed.has_errors()) {
            return 1;
        }
    }

    renderer::shaders::ShaderCompileConfig config;
    config.output_root = output_path.parent_path();
    config.manifest_relative_path = output_path.filename();
    config.profile = profile;
    auto compiled = renderer::shaders::ShaderCompiler::compile(catalog, config);
    if (!compiled) {
        core::log(core::LogLevel::error, compiled.error().message);
        return 1;
    }

    for (const auto& diagnostic : compiled.value().diagnostics) {
        log_diagnostic(diagnostic);
    }
    if (inspect_after_compile) {
        const auto inspection = debug::Inspector::inspect(compiled.value());
        std::cout << debug::Inspector::render_text(inspection);
        if (inspection.has_errors()) {
            return 1;
        }
    }
    if (compiled.value().has_errors()) {
        return 1;
    }

    core::log(core::LogLevel::info,
              "Compiled shaders: " + std::to_string(compiled.value().compiled_shader_count) +
                  " files, " + std::to_string(compiled.value().compiled_payload_bytes) +
                  " source bytes");
    core::log(core::LogLevel::info, "Shader compile profile: " + config.profile);
    core::log(core::LogLevel::info,
              "Shader manifest records: " + std::to_string(compiled.value().records.size()));
    core::log(core::LogLevel::info, "Wrote " + compiled.value().manifest_path.string());
    return 0;
}
