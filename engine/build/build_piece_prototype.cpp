#include "engine/build/build_piece_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <string>
#include <utility>
#include <vector>

namespace heartstead::build {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

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

[[nodiscard]] core::Result<std::vector<std::string>>
parse_token_list(const modding::GenericPrototype& prototype, std::string_view key) {
    const auto* value = field(prototype, key);
    std::vector<std::string> tokens;
    if (value == nullptr || value->empty()) {
        return core::Result<std::vector<std::string>>::success(std::move(tokens));
    }

    for (const auto token : split(*value, ',')) {
        if (!core::is_valid_local_id(token)) {
            return core::Result<std::vector<std::string>>::failure(
                "build_piece_prototype.invalid_token",
                std::string(key) + " contains invalid token: " + std::string(token));
        }
        tokens.emplace_back(token);
    }
    return core::Result<std::vector<std::string>>::success(std::move(tokens));
}

} // namespace

networks::NetworkKind network_kind_for_build_port(std::string_view port_name) noexcept {
    return networks::network_kind_for_port_name(port_name);
}

core::Result<BuildPieceRecord>
build_piece_record_from_prototype(const modding::GenericPrototype& prototype,
                                  core::SaveId object_id, Transform transform) {
    if (prototype.kind != modding::PrototypeKinds::build_piece) {
        return core::Result<BuildPieceRecord>::failure("build_piece_prototype.kind_mismatch",
                                                       "prototype is not a build piece");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<BuildPieceRecord>::failure("build_piece_prototype.invalid_id",
                                                       "build piece prototype id is invalid");
    }

    auto material_tags = parse_token_list(prototype, "material_tags");
    auto room_tags = parse_token_list(prototype, "room_contribution_tags");
    auto network_port_names = parse_token_list(prototype, "network_ports");
    if (!material_tags) {
        return core::Result<BuildPieceRecord>::failure(material_tags.error().code,
                                                       material_tags.error().message);
    }
    if (!room_tags) {
        return core::Result<BuildPieceRecord>::failure(room_tags.error().code,
                                                       room_tags.error().message);
    }
    if (!network_port_names) {
        return core::Result<BuildPieceRecord>::failure(network_port_names.error().code,
                                                       network_port_names.error().message);
    }

    BuildPieceRecord record;
    record.object_id = object_id;
    record.prototype_id = prototype.id;
    record.transform = transform;
    record.material_tags = std::move(material_tags).value();
    record.room_contribution_tags = std::move(room_tags).value();
    record.network_ports.reserve(network_port_names.value().size());
    for (const auto& name : network_port_names.value()) {
        record.network_ports.push_back({name, network_kind_for_build_port(name), 1});
    }

    auto status = record.validate();
    if (!status) {
        return core::Result<BuildPieceRecord>::failure(status.error().code, status.error().message);
    }
    return core::Result<BuildPieceRecord>::success(std::move(record));
}

} // namespace heartstead::build
