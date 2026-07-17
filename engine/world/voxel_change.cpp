#include "engine/world/voxel_change.hpp"

#include <charconv>
#include <limits>
#include <string>
#include <vector>

namespace heartstead::world {

namespace {

template <typename T>
[[nodiscard]] core::Result<T> parse_number(std::string_view text, std::string_view field) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return core::Result<T>::failure("voxel_change.invalid_number",
                                        "voxel change field is invalid: " +
                                            std::string(field));
    }
    return core::Result<T>::success(value);
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view text) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('|', start);
        fields.push_back(text.substr(start, end == std::string_view::npos ? text.size() - start
                                                                         : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return fields;
}

} // namespace

core::Status VoxelChangeRecord::validate() const {
    if (!chunk_identity.is_valid() || content_revision == 0 ||
        chunk_coord_for_block(position) != chunk_identity.coordinate) {
        return core::Status::failure(
            "voxel_change.invalid_identity",
            "voxel change must identify the resident chunk containing its block");
    }
    return core::Status::ok();
}

std::string VoxelChangeTextCodec::encode(const VoxelChangeRecord& change) {
    return std::to_string(change.position.x) + '|' + std::to_string(change.position.y) + '|' +
           std::to_string(change.position.z) + '|' + std::to_string(change.previous.type) + '|' +
           std::to_string(change.previous.light) + '|' +
           std::to_string(change.previous.state_bits) + '|' +
           std::to_string(change.previous.metadata_handle) + '|' +
           std::to_string(change.current.type) + '|' + std::to_string(change.current.light) + '|' +
           std::to_string(change.current.state_bits) + '|' +
           std::to_string(change.current.metadata_handle) + '|' +
           std::to_string(change.chunk_identity.coordinate.x) + '|' +
           std::to_string(change.chunk_identity.coordinate.y) + '|' +
           std::to_string(change.chunk_identity.coordinate.z) + '|' +
           std::to_string(change.chunk_identity.load_generation) + '|' +
           std::to_string(change.content_revision);
}

core::Result<VoxelChangeRecord> VoxelChangeTextCodec::decode(std::string_view payload) {
    const auto fields = split(payload);
    if (fields.size() != 16) {
        return core::Result<VoxelChangeRecord>::failure(
            "voxel_change.invalid_payload", "voxel change payload must contain 16 fields");
    }
    auto px = parse_number<std::int64_t>(fields[0], "position_x");
    auto py = parse_number<std::int64_t>(fields[1], "position_y");
    auto pz = parse_number<std::int64_t>(fields[2], "position_z");
    auto previous_type = parse_number<std::uint16_t>(fields[3], "previous_type");
    auto previous_light = parse_number<std::uint16_t>(fields[4], "previous_light");
    auto previous_state = parse_number<std::uint16_t>(fields[5], "previous_state");
    auto previous_metadata = parse_number<std::uint32_t>(fields[6], "previous_metadata");
    auto current_type = parse_number<std::uint16_t>(fields[7], "current_type");
    auto current_light = parse_number<std::uint16_t>(fields[8], "current_light");
    auto current_state = parse_number<std::uint16_t>(fields[9], "current_state");
    auto current_metadata = parse_number<std::uint32_t>(fields[10], "current_metadata");
    auto cx = parse_number<std::int64_t>(fields[11], "chunk_x");
    auto cy = parse_number<std::int64_t>(fields[12], "chunk_y");
    auto cz = parse_number<std::int64_t>(fields[13], "chunk_z");
    auto generation = parse_number<std::uint64_t>(fields[14], "load_generation");
    auto revision = parse_number<std::uint64_t>(fields[15], "content_revision");
    const bool parsed = px && py && pz && previous_type && previous_light && previous_state &&
                        previous_metadata && current_type && current_light && current_state &&
                        current_metadata && cx && cy && cz && generation && revision;
    if (!parsed || previous_light.value() > std::numeric_limits<std::uint8_t>::max() ||
        current_light.value() > std::numeric_limits<std::uint8_t>::max()) {
        return core::Result<VoxelChangeRecord>::failure(
            "voxel_change.invalid_payload", "voxel change payload fields are invalid");
    }
    VoxelChangeRecord result;
    result.position = {px.value(), py.value(), pz.value()};
    result.previous = {previous_type.value(), static_cast<std::uint8_t>(previous_light.value()),
                       previous_state.value(), previous_metadata.value()};
    result.current = {current_type.value(), static_cast<std::uint8_t>(current_light.value()),
                      current_state.value(), current_metadata.value()};
    result.chunk_identity = {{cx.value(), cy.value(), cz.value()}, generation.value()};
    result.content_revision = revision.value();
    auto status = result.validate();
    if (!status) {
        return core::Result<VoxelChangeRecord>::failure(status.error().code,
                                                        status.error().message);
    }
    return core::Result<VoxelChangeRecord>::success(result);
}

} // namespace heartstead::world
