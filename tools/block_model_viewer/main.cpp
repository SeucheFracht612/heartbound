#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/modding/mod_validation.hpp"
#include "engine/world/voxels/voxel_palette.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    return heartstead::core::run_process_entry(argv[0], [argc, argv] {
        using namespace heartstead;
        if (argc == 2 &&
            (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
            std::cout << "usage: " << argv[0] << " [source_root]\n";
            return 0;
        }
        if (argc > 2) {
            std::cerr << "usage: " << argv[0] << " [source_root]\n";
            return 2;
        }
        std::filesystem::path source_root = HEARTSTEAD_SOURCE_ROOT;
        if (argc > 1)
            source_root = argv[1];
        auto report = modding::ModValidation::validate(source_root / "mods");
        if (report.has_errors()) {
            core::log(core::LogLevel::error, "base mod validation failed");
            return 1;
        }
        auto palette = world::voxel_palette_from_prototypes(report.registry);
        if (!palette) {
            core::log(core::LogLevel::error, palette.error().message);
            return 1;
        }
        for (const auto* block : palette.value().definitions()) {
            const auto& model = palette.value().model_for(*block);
            core::log(core::LogLevel::info,
                      block->prototype_id.value() + " model=" + model.prototype_id.value() +
                          " bounds=" + std::to_string(model.render_bounds.min.x) + "," +
                          std::to_string(model.render_bounds.min.y) + "," +
                          std::to_string(model.render_bounds.min.z) + ".." +
                          std::to_string(model.render_bounds.max.x) + "," +
                          std::to_string(model.render_bounds.max.y) + "," +
                          std::to_string(model.render_bounds.max.z) +
                          " halo=" + std::to_string(model.neighbor_dependency_radius));
        }
        return 0;
    });
}
