#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/workpieces/workpiece_codec.hpp"
#include "engine/workpieces/workpiece_grid.hpp"
#include "engine/workpieces/workpiece_template.hpp"

#include <string>
#include <utility>

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        auto grid_result = workpieces::WorkpieceGrid::create({4, 4, 4});
        if (!grid_result) {
            core::log(core::LogLevel::error, grid_result.error().message);
            return 1;
        }

        auto grid = std::move(grid_result).value();
        const auto add_status = grid.apply(workpieces::WorkpieceOperation{
            workpieces::WorkpieceOperationKind::add_cell,
            {1, 1, 1},
            workpieces::WorkpieceCell::solid(7),
        });

        if (!add_status) {
            core::log(core::LogLevel::error, add_status.error().message);
            return 1;
        }

        const auto second_add_status = grid.apply(workpieces::WorkpieceOperation{
            workpieces::WorkpieceOperationKind::add_cell,
            {2, 1, 1},
            workpieces::WorkpieceCell::solid(7),
        });

        if (!second_add_status) {
            core::log(core::LogLevel::error, second_add_status.error().message);
            return 1;
        }

        workpieces::WorkpieceTemplate templ;
        templ.id = "sample_clay_strip";
        templ.shape = grid.shape();
        templ.required_cells.push_back({{1, 1, 1}, workpieces::WorkpieceCell::solid(7)});
        templ.required_cells.push_back({{2, 1, 1}, workpieces::WorkpieceCell::solid(7)});
        templ.forbidden_cells.push_back({0, 0, 0});
        templ.strict = true;

        const auto validation = workpieces::WorkpieceTemplateMatcher::match(grid, templ);
        if (!validation.valid) {
            core::log(core::LogLevel::error, "Workpiece did not match sample template");
            return 1;
        }

        const auto encoded = workpieces::WorkpieceGridTextCodec::encode(grid);
        auto decoded = workpieces::WorkpieceGridTextCodec::decode(encoded);
        if (!decoded) {
            core::log(core::LogLevel::error, decoded.error().message);
            return 1;
        }

        core::log(core::LogLevel::info,
                  "Workpiece operations applied: " + std::to_string(grid.history().size()));
        core::log(core::LogLevel::info,
                  "Occupied cells after operations: " + std::to_string(grid.occupied_count()));
        core::log(core::LogLevel::info,
                  "Decoded occupied cells: " + std::to_string(decoded.value().occupied_count()));

        return 0;
    });
}
