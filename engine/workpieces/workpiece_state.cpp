#include "engine/workpieces/workpiece_state.hpp"

#include "engine/core/hash.hpp"
#include "engine/workpieces/pattern_library.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::workpieces {

namespace {

[[nodiscard]] std::size_t cell_count(WorkpieceGridShape shape) noexcept {
    return static_cast<std::size_t>(shape.width) * shape.height * shape.depth;
}
[[nodiscard]] std::size_t word_count(WorkpieceGridShape shape) noexcept {
    return (cell_count(shape) + 63U) / 64U;
}
void set(std::vector<std::uint64_t>& words, std::size_t index) {
    words[index / 64U] |= std::uint64_t{1} << (index % 64U);
}
[[nodiscard]] bool get(const std::vector<std::uint64_t>& words, std::size_t index) noexcept {
    return index / 64U < words.size() &&
           (words[index / 64U] & (std::uint64_t{1} << (index % 64U))) != 0;
}
[[nodiscard]] std::string words(const std::vector<std::uint64_t>& values) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            output << ',';
        output << std::setw(16) << values[index];
    }
    return output.str();
}
[[nodiscard]] core::Result<std::vector<std::uint64_t>> parse_words(std::string_view value) {
    std::vector<std::uint64_t> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto token =
            end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start);
        std::uint64_t parsed = 0;
        const auto [ptr, error] =
            std::from_chars(token.data(), token.data() + token.size(), parsed, 16);
        if (error != std::errc{} || ptr != token.data() + token.size())
            return core::Result<std::vector<std::uint64_t>>::failure(
                "workpiece_state.invalid_mask", "workpiece mask contains invalid hex");
        result.push_back(parsed);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return core::Result<std::vector<std::uint64_t>>::success(std::move(result));
}

} // namespace

core::Result<WorkpieceServerState>
WorkpieceServerState::generate(WorkpieceGridShape shape, std::uint64_t seed,
                               std::uint16_t flaw_chance_per_mille) {
    if (flaw_chance_per_mille > 1000 || !WorkpieceGrid::create(shape))
        return core::Result<WorkpieceServerState>::failure(
            "workpiece_state.invalid_generation",
            "workpiece state generation parameters are invalid");
    WorkpieceServerState state;
    state.blob_mask.resize(word_count(shape));
    state.hidden_flaw_mask.resize(word_count(shape));
    state.revealed_mask.resize(word_count(shape));
    const auto count = cell_count(shape);
    for (std::size_t index = 0; index < count; ++index) {
        core::StableHash64 hash;
        hash.add_u64_le(seed);
        hash.add_u64_le(index);
        const auto noise = hash.value();
        const auto x = index % shape.width;
        const auto edge = x == 0 || x + 1U == shape.width;
        if (!edge || noise % 1000U > 180U)
            set(state.blob_mask, index);
        if (get(state.blob_mask, index) && noise % 1000U < flaw_chance_per_mille)
            set(state.hidden_flaw_mask, index);
    }
    auto status = state.validate(shape);
    if (!status)
        return core::Result<WorkpieceServerState>::failure(status.error().code,
                                                           status.error().message);
    return core::Result<WorkpieceServerState>::success(std::move(state));
}

core::Status WorkpieceServerState::validate(WorkpieceGridShape shape) const {
    const auto expected = word_count(shape);
    if (expected == 0 || blob_mask.size() != expected || hidden_flaw_mask.size() != expected ||
        revealed_mask.size() != expected)
        return core::Status::failure("workpiece_state.mask_size_mismatch",
                                     "workpiece masks do not match grid shape");
    for (std::size_t index = 0; index < expected; ++index)
        if ((hidden_flaw_mask[index] & ~blob_mask[index]) != 0 ||
            (revealed_mask[index] & ~blob_mask[index]) != 0)
            return core::Status::failure("workpiece_state.invalid_mask",
                                         "flaw and reveal masks must be inside blob mask");
    const auto used_bits = cell_count(shape) % 64U;
    if (used_bits != 0) {
        const auto valid_tail = (std::uint64_t{1} << used_bits) - 1U;
        if (((blob_mask.back() | hidden_flaw_mask.back() | revealed_mask.back()) & ~valid_tail) !=
            0)
            return core::Status::failure("workpiece_state.invalid_mask_tail",
                                         "workpiece masks set bits outside the grid");
    }
    if (!output_metadata.classification.empty() &&
        !core::is_valid_local_id(output_metadata.classification))
        return core::Status::failure("workpiece_state.invalid_classification",
                                     "workpiece output classification is invalid");
    for (const auto& [key, _] : output_metadata.measurements)
        if (!core::is_valid_local_id(key))
            return core::Status::failure("workpiece_state.invalid_measurement",
                                         "workpiece measurement key is invalid");
    return core::Status::ok();
}

bool WorkpieceServerState::in_blob(std::size_t cell_index) const noexcept {
    return get(blob_mask, cell_index);
}
bool WorkpieceServerState::flaw_revealed(std::size_t cell_index) const noexcept {
    return get(hidden_flaw_mask, cell_index) && get(revealed_mask, cell_index);
}
core::Result<bool> WorkpieceServerState::reveal(std::size_t cell_index) {
    if (!in_blob(cell_index))
        return core::Result<bool>::failure("workpiece_state.reveal_outside_blob",
                                           "cannot reveal outside blob mask");
    const auto previous = get(revealed_mask, cell_index);
    set(revealed_mask, cell_index);
    return core::Result<bool>::success(!previous);
}

std::string WorkpieceServerStateTextCodec::encode(const WorkpieceServerState& state,
                                                  WorkpieceGridShape shape) {
    std::ostringstream output;
    output << "heartstead.workpiece_server_state.v1\nshape=" << shape.width << '|' << shape.height
           << '|' << shape.depth << "\nblob=" << words(state.blob_mask)
           << "\nflaws=" << words(state.hidden_flaw_mask)
           << "\nrevealed=" << words(state.revealed_mask)
           << "\nclassification=" << state.output_metadata.classification
           << "\nmass=" << state.output_metadata.mass_units
           << "\nbase_closed=" << (state.output_metadata.base_closed ? 1 : 0)
           << "\nthin_walls=" << (state.output_metadata.thin_walls ? 1 : 0) << '\n';
    for (const auto& [key, value] : state.output_metadata.measurements)
        output << "measurement=" << key << '|' << value << '\n';
    output << "end\n";
    return output.str();
}

core::Result<WorkpieceServerState> WorkpieceServerStateTextCodec::decode(std::string_view text,
                                                                         WorkpieceGridShape shape) {
    if (text.size() > 1024U * 1024U)
        return core::Result<WorkpieceServerState>::failure(
            "workpiece_state.too_large", "workpiece server state exceeds one MiB");
    if (!text.starts_with("heartstead.workpiece_server_state.v1\n") || !text.ends_with("end\n"))
        return core::Result<WorkpieceServerState>::failure(
            "workpiece_state.invalid_codec", "workpiece server state framing is invalid");
    std::map<std::string, std::string_view> fields;
    std::vector<std::string_view> measurements;
    auto start = text.find('\n') + 1U;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos)
            break;
        const auto line = text.substr(start, end - start);
        start = end + 1U;
        if (line == "end")
            break;
        const auto separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0)
            return core::Result<WorkpieceServerState>::failure("workpiece_state.invalid_codec",
                                                               "workpiece state line is malformed");
        const auto key = std::string(line.substr(0, separator));
        const auto value = line.substr(separator + 1U);
        if (key == "measurement") {
            if (measurements.size() >= 256)
                return core::Result<WorkpieceServerState>::failure(
                    "workpiece_state.too_many_measurements",
                    "workpiece state contains too many measurements");
            measurements.push_back(value);
        } else if (key != "shape" && key != "blob" && key != "flaws" && key != "revealed" &&
                   key != "classification" && key != "mass" && key != "base_closed" &&
                   key != "thin_walls") {
            return core::Result<WorkpieceServerState>::failure(
                "workpiece_state.unknown_field", "workpiece state contains an unknown field");
        } else if (!fields.emplace(key, value).second) {
            return core::Result<WorkpieceServerState>::failure(
                "workpiece_state.duplicate_field", "workpiece state field is duplicated");
        }
    }
    const auto field_value = [&fields](std::string_view key) -> std::optional<std::string_view> {
        const auto found = fields.find(std::string(key));
        return found == fields.end() ? std::nullopt : std::optional{found->second};
    };
    auto encoded_shape = field_value("shape");
    auto blob_text = field_value("blob");
    auto flaws_text = field_value("flaws");
    auto revealed_text = field_value("revealed");
    auto mass_text = field_value("mass");
    auto base_text = field_value("base_closed");
    auto thin_text = field_value("thin_walls");
    if (!encoded_shape || !blob_text || !flaws_text || !revealed_text || !mass_text || !base_text ||
        !thin_text)
        return core::Result<WorkpieceServerState>::failure("workpiece_state.incomplete",
                                                           "workpiece server state is incomplete");
    const auto expected_shape = std::to_string(shape.width) + '|' + std::to_string(shape.height) +
                                '|' + std::to_string(shape.depth);
    if (*encoded_shape != expected_shape)
        return core::Result<WorkpieceServerState>::failure(
            "workpiece_state.shape_mismatch",
            "workpiece server state shape does not match its owning grid");
    auto blob = parse_words(*blob_text);
    auto flaws = parse_words(*flaws_text);
    auto revealed = parse_words(*revealed_text);
    std::uint64_t mass = 0;
    const auto [mass_end, mass_error] =
        std::from_chars(mass_text->data(), mass_text->data() + mass_text->size(), mass);
    if (!blob || !flaws || !revealed || mass_error != std::errc{} ||
        mass_end != mass_text->data() + mass_text->size())
        return core::Result<WorkpieceServerState>::failure(
            "workpiece_state.invalid_codec", "workpiece server state fields are invalid");
    if ((*base_text != "0" && *base_text != "1") || (*thin_text != "0" && *thin_text != "1"))
        return core::Result<WorkpieceServerState>::failure("workpiece_state.invalid_codec",
                                                           "workpiece state boolean is invalid");
    WorkpieceServerState state;
    state.blob_mask = std::move(blob).value();
    state.hidden_flaw_mask = std::move(flaws).value();
    state.revealed_mask = std::move(revealed).value();
    state.output_metadata.mass_units = mass;
    state.output_metadata.base_closed = *base_text == "1";
    state.output_metadata.thin_walls = *thin_text == "1";
    if (auto classification = field_value("classification"))
        state.output_metadata.classification = std::string(*classification);
    for (const auto encoded : measurements) {
        const auto separator = encoded.find('|');
        if (separator == std::string_view::npos || separator == 0)
            return core::Result<WorkpieceServerState>::failure(
                "workpiece_state.invalid_measurement", "workpiece measurement is malformed");
        std::int64_t parsed = 0;
        const auto number = encoded.substr(separator + 1U);
        const auto [end, error] =
            std::from_chars(number.data(), number.data() + number.size(), parsed);
        const auto key = std::string(encoded.substr(0, separator));
        if (!core::is_valid_local_id(key) || error != std::errc{} ||
            end != number.data() + number.size() ||
            !state.output_metadata.measurements.emplace(key, parsed).second)
            return core::Result<WorkpieceServerState>::failure(
                "workpiece_state.invalid_measurement", "workpiece measurement is invalid");
    }
    auto status = state.validate(shape);
    if (!status)
        return core::Result<WorkpieceServerState>::failure(status.error().code,
                                                           status.error().message);
    return core::Result<WorkpieceServerState>::success(std::move(state));
}

core::Result<WorkpieceFinishResult>
finish_workpiece(const WorkpieceGrid& grid, const WorkpieceServerState* server_state,
                 const PatternLibrary& patterns, const core::PrototypeId& material_id,
                 const std::optional<core::PrototypeId>& requested_pattern) {
    if (!material_id.is_valid()) {
        return core::Result<WorkpieceFinishResult>::failure(
            "workpiece_finish.invalid_material", "finishing requires a material prototype");
    }
    if (server_state != nullptr) {
        auto status = server_state->validate(grid.shape());
        if (!status) {
            return core::Result<WorkpieceFinishResult>::failure(status.error().code,
                                                                status.error().message);
        }
    }

    auto matched = patterns.match(grid, material_id);
    if (!matched.has_value() || (requested_pattern.has_value() && *requested_pattern != *matched)) {
        return core::Result<WorkpieceFinishResult>::failure(
            "workpiece_finish.pattern_mismatch",
            "server validation could not match the finished workpiece to the requested pattern");
    }
    const auto* pattern = patterns.find(*matched);
    if (pattern == nullptr) {
        return core::Result<WorkpieceFinishResult>::failure(
            "workpiece_finish.missing_pattern", "matched workpiece pattern is unavailable");
    }

    WorkpieceFinishResult result;
    result.pattern_id = *matched;
    result.output_prototype_id = pattern->output_prototype_id;
    result.metadata.classification = pattern->negative_mould ? "negative_mould" : "pattern_match";
    result.metadata.mass_units = grid.occupied_count();

    const auto shape = grid.shape();
    bool any_above_base = false;
    bool closed_base = true;
    std::uint64_t exposed_faces = 0;
    std::uint64_t thin_cells = 0;
    const auto occupied = [&grid, shape](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= shape.width || y >= shape.height || z >= shape.depth) {
            return false;
        }
        auto cell = grid.get({static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                              static_cast<std::uint16_t>(z)});
        return cell && cell.value().is_occupied();
    };
    constexpr std::array<std::array<int, 3>, 6> neighbour_offsets{{
        {{1, 0, 0}},
        {{-1, 0, 0}},
        {{0, 1, 0}},
        {{0, -1, 0}},
        {{0, 0, 1}},
        {{0, 0, -1}},
    }};
    for (std::uint16_t z = 0; z < shape.depth; ++z) {
        for (std::uint16_t y = 0; y < shape.height; ++y) {
            for (std::uint16_t x = 0; x < shape.width; ++x) {
                if (!occupied(x, y, z))
                    continue;
                if (y > 0) {
                    any_above_base = true;
                    closed_base = closed_base && occupied(x, 0, z);
                }
                std::uint32_t neighbours = 0;
                for (const auto offset : neighbour_offsets) {
                    if (occupied(static_cast<int>(x) + offset[0], static_cast<int>(y) + offset[1],
                                 static_cast<int>(z) + offset[2])) {
                        ++neighbours;
                    } else {
                        ++exposed_faces;
                    }
                }
                thin_cells += neighbours <= 2 ? 1U : 0U;
            }
        }
    }
    result.metadata.base_closed = !any_above_base || closed_base;
    result.metadata.thin_walls = thin_cells > 0;
    result.metadata.measurements["occupied_cells"] =
        static_cast<std::int64_t>(grid.occupied_count());
    result.metadata.measurements["exposed_faces"] = static_cast<std::int64_t>(exposed_faces);
    result.metadata.measurements["thin_cells"] = static_cast<std::int64_t>(thin_cells);
    result.byproduct_units = static_cast<std::uint32_t>(grid.history().size());
    return core::Result<WorkpieceFinishResult>::success(std::move(result));
}

} // namespace heartstead::workpieces
