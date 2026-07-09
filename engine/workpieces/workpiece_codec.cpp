#include "engine/workpieces/workpiece_codec.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::workpieces {

namespace {

constexpr std::string_view magic = "heartstead.workpiece_grid.v1";

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure("workpiece_codec.invalid_number",
                                                    "invalid numeric workpiece field: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::uint16_t> parse_u16(std::string_view value,
                                                    std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint16_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
        return core::Result<std::uint16_t>::failure("workpiece_codec.number_out_of_range",
                                                    "numeric workpiece field too large: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint16_t>::success(static_cast<std::uint16_t>(parsed.value()));
}

[[nodiscard]] core::Result<std::uint8_t> parse_u8(std::string_view value,
                                                  std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint8_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::uint8_t>::max()) {
        return core::Result<std::uint8_t>::failure("workpiece_codec.number_out_of_range",
                                                   "numeric workpiece field too large: " +
                                                       std::string(field_name));
    }
    return core::Result<std::uint8_t>::success(static_cast<std::uint8_t>(parsed.value()));
}

[[nodiscard]] core::Result<WorkpieceGridShape> parse_shape(std::string_view value) {
    const auto parts = split(value, '|');
    if (parts.size() != 3) {
        return core::Result<WorkpieceGridShape>::failure("workpiece_codec.invalid_shape",
                                                         "shape must contain width, height, depth");
    }

    auto width = parse_u16(parts[0], "width");
    auto height = parse_u16(parts[1], "height");
    auto depth = parse_u16(parts[2], "depth");
    if (!width) {
        return core::Result<WorkpieceGridShape>::failure(width.error().code, width.error().message);
    }
    if (!height) {
        return core::Result<WorkpieceGridShape>::failure(height.error().code,
                                                         height.error().message);
    }
    if (!depth) {
        return core::Result<WorkpieceGridShape>::failure(depth.error().code, depth.error().message);
    }

    return core::Result<WorkpieceGridShape>::success(
        WorkpieceGridShape{width.value(), height.value(), depth.value()});
}

[[nodiscard]] core::Status apply_cell(WorkpieceGrid& grid, std::string_view value) {
    const auto parts = split(value, '|');
    if (parts.size() != 5) {
        return core::Status::failure("workpiece_codec.invalid_cell",
                                     "cell must contain x, y, z, material, occupancy");
    }

    auto x = parse_u16(parts[0], "x");
    auto y = parse_u16(parts[1], "y");
    auto z = parse_u16(parts[2], "z");
    auto material = parse_u16(parts[3], "material");
    auto occupancy = parse_u8(parts[4], "occupancy");
    if (!x) {
        return core::Status::failure(x.error().code, x.error().message);
    }
    if (!y) {
        return core::Status::failure(y.error().code, y.error().message);
    }
    if (!z) {
        return core::Status::failure(z.error().code, z.error().message);
    }
    if (!material) {
        return core::Status::failure(material.error().code, material.error().message);
    }
    if (!occupancy) {
        return core::Status::failure(occupancy.error().code, occupancy.error().message);
    }

    return grid.apply(WorkpieceOperation{WorkpieceOperationKind::set_cell,
                                         {x.value(), y.value(), z.value()},
                                         {material.value(), occupancy.value()}});
}

} // namespace

std::string WorkpieceGridTextCodec::encode(const WorkpieceGrid& grid) {
    std::ostringstream output;
    const auto shape = grid.shape();
    output << magic << '\n';
    output << "shape=" << shape.width << '|' << shape.height << '|' << shape.depth << '\n';

    for (std::uint16_t z = 0; z < shape.depth; ++z) {
        for (std::uint16_t y = 0; y < shape.height; ++y) {
            for (std::uint16_t x = 0; x < shape.width; ++x) {
                auto cell = grid.get({x, y, z});
                if (cell && cell.value().is_occupied()) {
                    output << "cell=" << x << '|' << y << '|' << z << '|' << cell.value().material
                           << '|' << static_cast<unsigned int>(cell.value().occupancy) << '\n';
                }
            }
        }
    }

    output << "end\n";
    return output.str();
}

core::Result<WorkpieceGrid> WorkpieceGridTextCodec::decode(std::string_view text) {
    bool saw_magic = false;
    bool saw_end = false;
    std::optional<WorkpieceGrid> grid;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!saw_magic) {
            if (line != magic) {
                return core::Result<WorkpieceGrid>::failure(
                    "workpiece_codec.invalid_magic",
                    "workpiece grid does not start with expected magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                return core::Result<WorkpieceGrid>::failure(
                    "workpiece_codec.invalid_line",
                    "workpiece grid line is missing key/value separator");
            }

            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "shape") {
                auto shape = parse_shape(value);
                if (!shape) {
                    return core::Result<WorkpieceGrid>::failure(shape.error().code,
                                                                shape.error().message);
                }
                auto created = WorkpieceGrid::create(shape.value());
                if (!created) {
                    return core::Result<WorkpieceGrid>::failure(created.error().code,
                                                                created.error().message);
                }
                grid = std::move(created).value();
            } else if (key == "cell") {
                if (!grid.has_value()) {
                    return core::Result<WorkpieceGrid>::failure(
                        "workpiece_codec.cell_before_shape",
                        "cell records must appear after the shape record");
                }
                auto status = apply_cell(*grid, value);
                if (!status) {
                    return core::Result<WorkpieceGrid>::failure(status.error().code,
                                                                status.error().message);
                }
            } else {
                return core::Result<WorkpieceGrid>::failure("workpiece_codec.unknown_key",
                                                            "unknown workpiece grid key: " +
                                                                std::string(key));
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || !grid.has_value()) {
        return core::Result<WorkpieceGrid>::failure("workpiece_codec.incomplete",
                                                    "workpiece grid is missing required records");
    }

    return core::Result<WorkpieceGrid>::success(std::move(*grid));
}

} // namespace heartstead::workpieces
