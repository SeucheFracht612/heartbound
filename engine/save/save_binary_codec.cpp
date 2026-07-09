#include "engine/save/save_binary_codec.hpp"

#include "engine/math/vector.hpp"

#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::save {

namespace {

constexpr std::string_view magic = "HSTDSAVE";
constexpr std::uint32_t binary_version = 6;
constexpr std::uint32_t minimum_supported_binary_version = 2;

class BinaryWriter {
  public:
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] std::vector<std::uint8_t> take() && noexcept {
        return std::move(bytes_);
    }

    void write_u8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void write_bool(bool value) {
        write_u8(value ? 1 : 0);
    }

    void write_u16(std::uint16_t value) {
        write_u8(static_cast<std::uint8_t>(value & 0xffu));
        write_u8(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    }

    void write_u32(std::uint32_t value) {
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            write_u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void write_u64(std::uint64_t value) {
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            write_u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void write_i32(std::int32_t value) {
        write_u32(static_cast<std::uint32_t>(value));
    }

    void write_i64(std::int64_t value) {
        write_u64(static_cast<std::uint64_t>(value));
    }

    void write_double(double value) {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        write_u64(bits);
    }

    void write_string(std::string_view value) {
        write_count(value.size(), "string");
        for (const auto byte : value) {
            write_u8(static_cast<std::uint8_t>(byte));
        }
    }

    void write_count(std::size_t value, std::string_view label) {
        (void)label;
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            value = std::numeric_limits<std::uint32_t>::max();
        }
        write_u32(static_cast<std::uint32_t>(value));
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
  public:
    explicit BinaryReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool eof() const noexcept {
        return offset_ == bytes_.size();
    }

    [[nodiscard]] core::Result<std::uint8_t> read_u8(std::string_view label) {
        if (remaining() < 1) {
            return failure<std::uint8_t>(label);
        }
        return core::Result<std::uint8_t>::success(bytes_[offset_++]);
    }

    [[nodiscard]] core::Result<bool> read_bool(std::string_view label) {
        auto value = read_u8(label);
        if (!value) {
            return core::Result<bool>::failure(value.error().code, value.error().message);
        }
        if (value.value() > 1) {
            return core::Result<bool>::failure("save_binary.invalid_bool",
                                               "binary boolean field is not 0 or 1: " +
                                                   std::string(label));
        }
        return core::Result<bool>::success(value.value() == 1);
    }

    [[nodiscard]] core::Result<std::uint16_t> read_u16(std::string_view label) {
        std::uint16_t result = 0;
        for (std::uint32_t shift = 0; shift < 16; shift += 8) {
            auto byte = read_u8(label);
            if (!byte) {
                return core::Result<std::uint16_t>::failure(byte.error().code,
                                                            byte.error().message);
            }
            result = static_cast<std::uint16_t>(
                result | (static_cast<std::uint16_t>(byte.value()) << shift));
        }
        return core::Result<std::uint16_t>::success(result);
    }

    [[nodiscard]] core::Result<std::uint32_t> read_u32(std::string_view label) {
        std::uint32_t result = 0;
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            auto byte = read_u8(label);
            if (!byte) {
                return core::Result<std::uint32_t>::failure(byte.error().code,
                                                            byte.error().message);
            }
            result |= static_cast<std::uint32_t>(byte.value()) << shift;
        }
        return core::Result<std::uint32_t>::success(result);
    }

    [[nodiscard]] core::Result<std::uint64_t> read_u64(std::string_view label) {
        std::uint64_t result = 0;
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            auto byte = read_u8(label);
            if (!byte) {
                return core::Result<std::uint64_t>::failure(byte.error().code,
                                                            byte.error().message);
            }
            result |= static_cast<std::uint64_t>(byte.value()) << shift;
        }
        return core::Result<std::uint64_t>::success(result);
    }

    [[nodiscard]] core::Result<std::int32_t> read_i32(std::string_view label) {
        auto value = read_u32(label);
        if (!value) {
            return core::Result<std::int32_t>::failure(value.error().code, value.error().message);
        }
        return core::Result<std::int32_t>::success(static_cast<std::int32_t>(value.value()));
    }

    [[nodiscard]] core::Result<std::int64_t> read_i64(std::string_view label) {
        auto value = read_u64(label);
        if (!value) {
            return core::Result<std::int64_t>::failure(value.error().code, value.error().message);
        }
        return core::Result<std::int64_t>::success(static_cast<std::int64_t>(value.value()));
    }

    [[nodiscard]] core::Result<double> read_double(std::string_view label) {
        auto bits = read_u64(label);
        if (!bits) {
            return core::Result<double>::failure(bits.error().code, bits.error().message);
        }
        double value = 0.0;
        auto raw = bits.value();
        static_assert(sizeof(raw) == sizeof(value));
        std::memcpy(&value, &raw, sizeof(value));
        return core::Result<double>::success(value);
    }

    [[nodiscard]] core::Result<std::string> read_string(std::string_view label) {
        auto size = read_u32(label);
        if (!size) {
            return core::Result<std::string>::failure(size.error().code, size.error().message);
        }
        if (remaining() < size.value()) {
            return failure<std::string>(label);
        }

        std::string result(reinterpret_cast<const char*>(bytes_.data() + offset_), size.value());
        offset_ += size.value();
        return core::Result<std::string>::success(std::move(result));
    }

  private:
    template <typename T> [[nodiscard]] core::Result<T> failure(std::string_view label) const {
        return core::Result<T>::failure("save_binary.truncated",
                                        "binary save ended while reading " + std::string(label));
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] std::uint8_t construction_state_id(build::ConstructionState state) noexcept {
    return static_cast<std::uint8_t>(state);
}

[[nodiscard]] core::Result<build::ConstructionState> read_construction_state(BinaryReader& reader) {
    auto id = reader.read_u8("construction_state");
    if (!id) {
        return core::Result<build::ConstructionState>::failure(id.error().code, id.error().message);
    }
    switch (id.value()) {
    case 0:
        return core::Result<build::ConstructionState>::success(build::ConstructionState::planned);
    case 1:
        return core::Result<build::ConstructionState>::success(
            build::ConstructionState::under_construction);
    case 2:
        return core::Result<build::ConstructionState>::success(build::ConstructionState::complete);
    case 3:
        return core::Result<build::ConstructionState>::success(build::ConstructionState::damaged);
    }
    return core::Result<build::ConstructionState>::failure(
        "save_binary.invalid_construction_state", "binary save has invalid construction state");
}

[[nodiscard]] std::uint8_t network_kind_id(networks::NetworkKind kind) noexcept {
    return static_cast<std::uint8_t>(kind);
}

[[nodiscard]] core::Result<networks::NetworkKind> read_network_kind(BinaryReader& reader) {
    auto id = reader.read_u8("network_kind");
    if (!id) {
        return core::Result<networks::NetworkKind>::failure(id.error().code, id.error().message);
    }
    switch (id.value()) {
    case 0:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::road);
    case 1:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::cart_access);
    case 2:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::storage_access);
    case 3:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::power);
    case 4:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::ward);
    case 5:
        return core::Result<networks::NetworkKind>::success(
            networks::NetworkKind::smoke_ventilation);
    case 6:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::water);
    case 7:
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::logistics);
    }
    return core::Result<networks::NetworkKind>::failure("save_binary.invalid_network_kind",
                                                        "binary save has invalid network kind");
}

[[nodiscard]] std::uint8_t entity_kind_id(entities::EntityKind kind) noexcept {
    return static_cast<std::uint8_t>(kind);
}

[[nodiscard]] core::Result<entities::EntityKind> read_entity_kind(BinaryReader& reader) {
    auto id = reader.read_u8("entity_kind");
    if (!id) {
        return core::Result<entities::EntityKind>::failure(id.error().code, id.error().message);
    }
    switch (id.value()) {
    case 0:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::player);
    case 1:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::creature);
    case 2:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::animal);
    case 3:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::cart);
    case 4:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::boat);
    case 5:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::dropped_item);
    case 6:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::projectile);
    case 7:
        return core::Result<entities::EntityKind>::success(entities::EntityKind::temporary_physics);
    }
    return core::Result<entities::EntityKind>::failure("save_binary.invalid_entity_kind",
                                                       "binary save has invalid entity kind");
}

[[nodiscard]] std::uint8_t process_state_id(processes::ProcessState state) noexcept {
    return static_cast<std::uint8_t>(state);
}

[[nodiscard]] core::Result<processes::ProcessState> read_process_state(BinaryReader& reader) {
    auto id = reader.read_u8("process_state");
    if (!id) {
        return core::Result<processes::ProcessState>::failure(id.error().code, id.error().message);
    }
    switch (id.value()) {
    case 0:
        return core::Result<processes::ProcessState>::success(processes::ProcessState::running);
    case 1:
        return core::Result<processes::ProcessState>::success(processes::ProcessState::interrupted);
    case 2:
        return core::Result<processes::ProcessState>::success(processes::ProcessState::complete);
    }
    return core::Result<processes::ProcessState>::failure("save_binary.invalid_process_state",
                                                          "binary save has invalid process state");
}

void write_prototype_id(BinaryWriter& writer, const core::PrototypeId& id) {
    writer.write_string(id.value());
}

[[nodiscard]] core::Result<core::PrototypeId> read_prototype_id(BinaryReader& reader,
                                                                std::string_view label) {
    auto value = reader.read_string(label);
    if (!value) {
        return core::Result<core::PrototypeId>::failure(value.error().code, value.error().message);
    }
    auto parsed = core::PrototypeId::parse(value.value());
    if (!parsed) {
        return core::Result<core::PrototypeId>::failure("save_binary.invalid_prototype",
                                                        "binary save prototype id is invalid");
    }
    return core::Result<core::PrototypeId>::success(parsed.value());
}

void write_string_list(BinaryWriter& writer, const std::vector<std::string>& values) {
    writer.write_count(values.size(), "string_list");
    for (const auto& value : values) {
        writer.write_string(value);
    }
}

[[nodiscard]] core::Result<std::vector<std::string>> read_string_list(BinaryReader& reader,
                                                                      std::string_view label) {
    auto count = reader.read_u32(label);
    if (!count) {
        return core::Result<std::vector<std::string>>::failure(count.error().code,
                                                               count.error().message);
    }
    std::vector<std::string> values;
    values.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto value = reader.read_string(label);
        if (!value) {
            return core::Result<std::vector<std::string>>::failure(value.error().code,
                                                                   value.error().message);
        }
        values.push_back(std::move(value).value());
    }
    return core::Result<std::vector<std::string>>::success(std::move(values));
}

void write_transform(BinaryWriter& writer, const build::Transform& transform) {
    writer.write_double(transform.position.x);
    writer.write_double(transform.position.y);
    writer.write_double(transform.position.z);
    writer.write_double(transform.rotation_degrees.x);
    writer.write_double(transform.rotation_degrees.y);
    writer.write_double(transform.rotation_degrees.z);
    writer.write_double(transform.scale.x);
    writer.write_double(transform.scale.y);
    writer.write_double(transform.scale.z);
}

void write_vec3(BinaryWriter& writer, math::Vec3d value) {
    writer.write_double(value.x);
    writer.write_double(value.y);
    writer.write_double(value.z);
}

[[nodiscard]] core::Result<math::Vec3d> read_vec3(BinaryReader& reader, std::string_view label) {
    auto x = reader.read_double(std::string(label) + "_x");
    auto y = reader.read_double(std::string(label) + "_y");
    auto z = reader.read_double(std::string(label) + "_z");
    if (!x || !y || !z) {
        return core::Result<math::Vec3d>::failure("save_binary.invalid_vec3",
                                                  "binary save has invalid vec3 fields");
    }
    return core::Result<math::Vec3d>::success({x.value(), y.value(), z.value()});
}

[[nodiscard]] core::Result<build::Transform> read_transform(BinaryReader& reader) {
    build::Transform transform;
    auto px = reader.read_double("position_x");
    auto py = reader.read_double("position_y");
    auto pz = reader.read_double("position_z");
    auto rx = reader.read_double("rotation_x");
    auto ry = reader.read_double("rotation_y");
    auto rz = reader.read_double("rotation_z");
    auto sx = reader.read_double("scale_x");
    auto sy = reader.read_double("scale_y");
    auto sz = reader.read_double("scale_z");
    if (!px || !py || !pz || !rx || !ry || !rz || !sx || !sy || !sz) {
        return core::Result<build::Transform>::failure("save_binary.invalid_transform",
                                                       "binary save has invalid transform fields");
    }
    transform.position = {px.value(), py.value(), pz.value()};
    transform.rotation_degrees = {rx.value(), ry.value(), rz.value()};
    transform.scale = {sx.value(), sy.value(), sz.value()};
    return core::Result<build::Transform>::success(transform);
}

void write_build_sockets(BinaryWriter& writer, const std::vector<build::BuildSocket>& sockets) {
    writer.write_count(sockets.size(), "build_sockets");
    for (const auto& socket : sockets) {
        writer.write_string(socket.name);
        writer.write_double(socket.local_position.x);
        writer.write_double(socket.local_position.y);
        writer.write_double(socket.local_position.z);
        writer.write_string(socket.tag);
    }
}

[[nodiscard]] core::Result<std::vector<build::BuildSocket>>
read_build_sockets(BinaryReader& reader) {
    auto count = reader.read_u32("build_sockets");
    if (!count) {
        return core::Result<std::vector<build::BuildSocket>>::failure(count.error().code,
                                                                      count.error().message);
    }
    std::vector<build::BuildSocket> sockets;
    sockets.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto name = reader.read_string("socket_name");
        auto x = reader.read_double("socket_x");
        auto y = reader.read_double("socket_y");
        auto z = reader.read_double("socket_z");
        auto tag = reader.read_string("socket_tag");
        if (!name || !x || !y || !z || !tag) {
            return core::Result<std::vector<build::BuildSocket>>::failure(
                "save_binary.invalid_socket", "binary save has invalid socket fields");
        }
        sockets.push_back(build::BuildSocket{
            std::move(name).value(), {x.value(), y.value(), z.value()}, std::move(tag).value()});
    }
    return core::Result<std::vector<build::BuildSocket>>::success(std::move(sockets));
}

void write_build_ports(BinaryWriter& writer, const std::vector<build::BuildNetworkPort>& ports) {
    writer.write_count(ports.size(), "build_ports");
    for (const auto& port : ports) {
        writer.write_string(port.name);
        writer.write_u8(network_kind_id(port.kind));
        writer.write_u32(port.capacity);
    }
}

[[nodiscard]] core::Result<std::vector<build::BuildNetworkPort>>
read_build_ports(BinaryReader& reader) {
    auto count = reader.read_u32("build_ports");
    if (!count) {
        return core::Result<std::vector<build::BuildNetworkPort>>::failure(count.error().code,
                                                                           count.error().message);
    }
    std::vector<build::BuildNetworkPort> ports;
    ports.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto name = reader.read_string("build_port_name");
        auto kind = read_network_kind(reader);
        auto capacity = reader.read_u32("build_port_capacity");
        if (!name || !kind || !capacity) {
            return core::Result<std::vector<build::BuildNetworkPort>>::failure(
                "save_binary.invalid_build_port", "binary save has invalid build port fields");
        }
        ports.push_back(
            build::BuildNetworkPort{std::move(name).value(), kind.value(), capacity.value()});
    }
    return core::Result<std::vector<build::BuildNetworkPort>>::success(std::move(ports));
}

void write_item_stacks(BinaryWriter& writer, const std::vector<items::ItemStack>& stacks) {
    writer.write_count(stacks.size(), "item_stacks");
    for (const auto& stack : stacks) {
        write_prototype_id(writer, stack.prototype_id);
        writer.write_u32(stack.count);
        writer.write_u32(stack.max_count);
        writer.write_u16(stack.quality);
    }
}

[[nodiscard]] core::Result<std::vector<items::ItemStack>> read_item_stacks(BinaryReader& reader) {
    auto count = reader.read_u32("item_stacks");
    if (!count) {
        return core::Result<std::vector<items::ItemStack>>::failure(count.error().code,
                                                                    count.error().message);
    }
    std::vector<items::ItemStack> stacks;
    stacks.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto prototype = read_prototype_id(reader, "item_prototype");
        auto item_count = reader.read_u32("item_count");
        auto max_count = reader.read_u32("item_max_count");
        auto quality = reader.read_u16("item_quality");
        if (!prototype || !item_count || !max_count || !quality) {
            return core::Result<std::vector<items::ItemStack>>::failure(
                "save_binary.invalid_item_stack", "binary save has invalid item stack fields");
        }
        stacks.push_back(items::ItemStack{prototype.value(), item_count.value(), max_count.value(),
                                          quality.value()});
    }
    return core::Result<std::vector<items::ItemStack>>::success(std::move(stacks));
}

void write_process_slots(BinaryWriter& writer, const std::vector<processes::ProcessSlot>& slots) {
    writer.write_count(slots.size(), "process_slots");
    for (const auto& slot : slots) {
        write_prototype_id(writer, slot.prototype_id);
        writer.write_u32(slot.count);
    }
}

[[nodiscard]] core::Result<std::vector<processes::ProcessSlot>>
read_process_slots(BinaryReader& reader) {
    auto count = reader.read_u32("process_slots");
    if (!count) {
        return core::Result<std::vector<processes::ProcessSlot>>::failure(count.error().code,
                                                                          count.error().message);
    }
    std::vector<processes::ProcessSlot> slots;
    slots.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto prototype = read_prototype_id(reader, "process_slot_prototype");
        auto slot_count = reader.read_u32("process_slot_count");
        if (!prototype || !slot_count) {
            return core::Result<std::vector<processes::ProcessSlot>>::failure(
                "save_binary.invalid_process_slot", "binary save has invalid process slot fields");
        }
        slots.push_back(processes::ProcessSlot{prototype.value(), slot_count.value()});
    }
    return core::Result<std::vector<processes::ProcessSlot>>::success(std::move(slots));
}

void write_assembly_parts(BinaryWriter& writer,
                          const std::vector<assemblies::AssemblyPart>& parts) {
    writer.write_count(parts.size(), "assembly_parts");
    for (const auto& part : parts) {
        writer.write_string(part.name);
        writer.write_u64(part.build_piece_id.value());
        write_prototype_id(writer, part.prototype_id);
    }
}

[[nodiscard]] core::Result<std::vector<assemblies::AssemblyPart>>
read_assembly_parts(BinaryReader& reader) {
    auto count = reader.read_u32("assembly_parts");
    if (!count) {
        return core::Result<std::vector<assemblies::AssemblyPart>>::failure(count.error().code,
                                                                            count.error().message);
    }
    std::vector<assemblies::AssemblyPart> parts;
    parts.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto name = reader.read_string("assembly_part_name");
        auto build_piece_id = reader.read_u64("assembly_part_build_piece_id");
        auto prototype = read_prototype_id(reader, "assembly_part_prototype");
        if (!name || !build_piece_id || !prototype) {
            return core::Result<std::vector<assemblies::AssemblyPart>>::failure(
                "save_binary.invalid_assembly_part",
                "binary save has invalid assembly part fields");
        }
        parts.push_back(assemblies::AssemblyPart{std::move(name).value(),
                                                 core::SaveId::from_value(build_piece_id.value()),
                                                 prototype.value()});
    }
    return core::Result<std::vector<assemblies::AssemblyPart>>::success(std::move(parts));
}

void write_assembly_ports(BinaryWriter& writer,
                          const std::vector<assemblies::AssemblyPort>& ports) {
    writer.write_count(ports.size(), "assembly_ports");
    for (const auto& port : ports) {
        writer.write_string(port.name);
        writer.write_u8(network_kind_id(port.kind));
        writer.write_u64(port.source_build_piece_id.value());
        writer.write_u32(port.capacity);
    }
}

[[nodiscard]] core::Result<std::vector<assemblies::AssemblyPort>>
read_assembly_ports(BinaryReader& reader) {
    auto count = reader.read_u32("assembly_ports");
    if (!count) {
        return core::Result<std::vector<assemblies::AssemblyPort>>::failure(count.error().code,
                                                                            count.error().message);
    }
    std::vector<assemblies::AssemblyPort> ports;
    ports.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto name = reader.read_string("assembly_port_name");
        auto kind = read_network_kind(reader);
        auto source = reader.read_u64("assembly_port_source_build_piece_id");
        auto capacity = reader.read_u32("assembly_port_capacity");
        if (!name || !kind || !source || !capacity) {
            return core::Result<std::vector<assemblies::AssemblyPort>>::failure(
                "save_binary.invalid_assembly_port",
                "binary save has invalid assembly port fields");
        }
        ports.push_back(assemblies::AssemblyPort{std::move(name).value(), kind.value(),
                                                 core::SaveId::from_value(source.value()),
                                                 capacity.value()});
    }
    return core::Result<std::vector<assemblies::AssemblyPort>>::success(std::move(ports));
}

void write_metadata(BinaryWriter& writer, const SaveMetadata& metadata) {
    writer.write_u32(metadata.schema_version);
    writer.write_string(metadata.game_version);
    writer.write_u64(metadata.world_seed);
    writer.write_u64(metadata.world_time);
    writer.write_count(metadata.enabled_mods.size(), "enabled_mods");
    for (const auto& mod : metadata.enabled_mods) {
        writer.write_string(mod.id);
        writer.write_string(mod.version);
        writer.write_string(mod.prototype_hash);
    }
    write_string_list(writer, metadata.migration_history);
}

[[nodiscard]] core::Result<SaveMetadata> read_metadata(BinaryReader& reader,
                                                       std::uint32_t version) {
    SaveMetadata metadata;
    auto schema = reader.read_u32("schema_version");
    auto game_version = reader.read_string("game_version");
    auto world_seed = reader.read_u64("world_seed");
    if (!schema || !game_version || !world_seed) {
        return core::Result<SaveMetadata>::failure("save_binary.invalid_metadata",
                                                   "binary save has invalid metadata fields");
    }
    metadata.schema_version = schema.value();
    metadata.game_version = std::move(game_version).value();
    metadata.world_seed = world_seed.value();
    if (version >= 6) {
        auto world_time = reader.read_u64("world_time");
        if (!world_time) {
            return core::Result<SaveMetadata>::failure(world_time.error().code,
                                                       world_time.error().message);
        }
        metadata.world_time = world_time.value();
    }
    auto mod_count = reader.read_u32("enabled_mods");
    if (!mod_count) {
        return core::Result<SaveMetadata>::failure("save_binary.invalid_metadata",
                                                   "binary save has invalid metadata fields");
    }
    metadata.enabled_mods.reserve(mod_count.value());
    for (std::uint32_t index = 0; index < mod_count.value(); ++index) {
        auto id = reader.read_string("mod_id");
        auto mod_version = reader.read_string("mod_version");
        auto hash = reader.read_string("prototype_hash");
        if (!id || !mod_version || !hash) {
            return core::Result<SaveMetadata>::failure("save_binary.invalid_mod_record",
                                                       "binary save has invalid mod record");
        }
        metadata.enabled_mods.push_back(
            {std::move(id).value(), std::move(mod_version).value(), std::move(hash).value()});
    }
    auto migration_history = read_string_list(reader, "migration_history");
    if (!migration_history) {
        return core::Result<SaveMetadata>::failure(migration_history.error().code,
                                                   migration_history.error().message);
    }
    metadata.migration_history = std::move(migration_history).value();
    auto status = metadata.validate();
    if (!status) {
        return core::Result<SaveMetadata>::failure(status.error().code, status.error().message);
    }
    return core::Result<SaveMetadata>::success(std::move(metadata));
}

[[nodiscard]] core::Result<std::int64_t>
read_chunk_coord_component(BinaryReader& reader, std::uint32_t version, std::string_view label) {
    if (version >= 5) {
        return reader.read_i64(label);
    }

    auto legacy = reader.read_i32(label);
    if (!legacy) {
        return core::Result<std::int64_t>::failure(legacy.error().code, legacy.error().message);
    }
    return core::Result<std::int64_t>::success(legacy.value());
}

template <typename T>
[[nodiscard]] core::Result<std::uint32_t> read_count(BinaryReader& reader, std::string_view label) {
    (void)sizeof(T);
    return reader.read_u32(label);
}

} // namespace

std::vector<std::uint8_t> SaveBinaryCodec::encode_snapshot(const SaveSnapshot& snapshot) {
    BinaryWriter writer;
    for (const auto byte : magic) {
        writer.write_u8(static_cast<std::uint8_t>(byte));
    }
    writer.write_u32(binary_version);
    write_metadata(writer, snapshot.metadata);

    writer.write_count(snapshot.chunk_edits.size(), "chunk_edits");
    for (const auto& chunk : snapshot.chunk_edits) {
        writer.write_i64(chunk.coord.x);
        writer.write_i64(chunk.coord.y);
        writer.write_i64(chunk.coord.z);
        writer.write_string(chunk.encoded_edit_delta);
    }

    writer.write_count(snapshot.build_pieces.size(), "build_pieces");
    for (const auto& build_piece : snapshot.build_pieces) {
        writer.write_u64(build_piece.object_id.value());
        write_prototype_id(writer, build_piece.prototype_id);
        write_transform(writer, build_piece.transform);
        writer.write_u8(construction_state_id(build_piece.construction_state));
        write_build_sockets(writer, build_piece.sockets);
        write_build_ports(writer, build_piece.network_ports);
        write_string_list(writer, build_piece.material_tags);
        write_string_list(writer, build_piece.room_contribution_tags);
    }

    writer.write_count(snapshot.entities.size(), "entities");
    for (const auto& entity : snapshot.entities) {
        writer.write_u64(entity.save_id.value());
        write_prototype_id(writer, entity.prototype_id);
        writer.write_u8(entity_kind_id(entity.kind));
        write_transform(writer, entity.transform);
        writer.write_bool(entity.sleeping);
        writer.write_string(entity.encoded_state);
    }

    writer.write_count(snapshot.inventories.size(), "inventories");
    for (const auto& inventory : snapshot.inventories) {
        writer.write_u64(inventory.owner_id.value());
        write_item_stacks(writer, inventory.stacks);
    }

    writer.write_count(snapshot.cargo_records.size(), "cargo_records");
    for (const auto& cargo_record : snapshot.cargo_records) {
        writer.write_u64(cargo_record.cargo_id.value());
        write_prototype_id(writer, cargo_record.prototype_id);
        write_vec3(writer, cargo_record.position);
        writer.write_u64(cargo_record.mass_grams);
        writer.write_u64(cargo_record.volume_milliliters);
        writer.write_i32(cargo_record.stability_per_mille);
        writer.write_u32(cargo_record.allowed_transport_modes.bits());
        write_string_list(writer, cargo_record.hazard_tags);
    }

    writer.write_count(snapshot.workpieces.size(), "workpieces");
    for (const auto& workpiece : snapshot.workpieces) {
        writer.write_u64(workpiece.workpiece_id.value());
        write_prototype_id(writer, workpiece.prototype_id);
        writer.write_u16(workpiece.shape.width);
        writer.write_u16(workpiece.shape.height);
        writer.write_u16(workpiece.shape.depth);
        writer.write_string(workpiece.encoded_cells);
    }

    writer.write_count(snapshot.assemblies.size(), "assemblies");
    for (const auto& assembly : snapshot.assemblies) {
        writer.write_u64(assembly.assembly_id.value());
        writer.write_u64(assembly.root_build_piece_id.value());
        write_prototype_id(writer, assembly.prototype_id);
        writer.write_bool(assembly.operating);
        write_assembly_parts(writer, assembly.parts);
        write_assembly_ports(writer, assembly.ports);
    }

    writer.write_count(snapshot.processes.size(), "processes");
    for (const auto& process : snapshot.processes) {
        writer.write_u64(process.process_id.value());
        writer.write_u64(process.owner_id.value());
        write_prototype_id(writer, process.prototype_id);
        writer.write_i64(process.start_time_ms);
        writer.write_i64(process.last_update_time_ms);
        writer.write_i64(process.required_effective_work_ms);
        writer.write_i64(process.accumulated_effective_work_ms);
        writer.write_u8(process_state_id(process.state));
        writer.write_string(process.interruption_reason);
        write_process_slots(writer, process.input_slots);
        write_process_slots(writer, process.output_slots);
    }

    writer.write_count(snapshot.mod_states.size(), "mod_states");
    for (const auto& mod_state : snapshot.mod_states) {
        writer.write_string(mod_state.mod_id);
        writer.write_string(mod_state.state_key);
        writer.write_string(mod_state.encoded_state);
    }

    return std::move(writer).take();
}

core::Result<SaveSnapshot> SaveBinaryCodec::decode_snapshot(std::span<const std::uint8_t> bytes) {
    BinaryReader reader(bytes);
    for (const auto expected : magic) {
        auto actual = reader.read_u8("magic");
        if (!actual) {
            return core::Result<SaveSnapshot>::failure(actual.error().code, actual.error().message);
        }
        if (actual.value() != static_cast<std::uint8_t>(expected)) {
            return core::Result<SaveSnapshot>::failure(
                "save_binary.invalid_magic", "binary save does not start with the expected magic");
        }
    }

    auto version = reader.read_u32("binary_version");
    if (!version) {
        return core::Result<SaveSnapshot>::failure(version.error().code, version.error().message);
    }
    if (version.value() < minimum_supported_binary_version || version.value() > binary_version) {
        return core::Result<SaveSnapshot>::failure("save_binary.unsupported_version",
                                                   "binary save version is unsupported");
    }

    SaveSnapshot snapshot;
    auto metadata = read_metadata(reader, version.value());
    if (!metadata) {
        return core::Result<SaveSnapshot>::failure(metadata.error().code, metadata.error().message);
    }
    snapshot.metadata = std::move(metadata).value();

    auto chunk_count = read_count<ChunkEditSaveRecord>(reader, "chunk_edits");
    if (!chunk_count) {
        return core::Result<SaveSnapshot>::failure(chunk_count.error().code,
                                                   chunk_count.error().message);
    }
    snapshot.chunk_edits.reserve(chunk_count.value());
    for (std::uint32_t index = 0; index < chunk_count.value(); ++index) {
        auto x = read_chunk_coord_component(reader, version.value(), "chunk_x");
        auto y = read_chunk_coord_component(reader, version.value(), "chunk_y");
        auto z = read_chunk_coord_component(reader, version.value(), "chunk_z");
        auto delta = reader.read_string("chunk_delta");
        if (!x || !y || !z || !delta) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_chunk",
                                                       "binary save has invalid chunk record");
        }
        snapshot.chunk_edits.push_back(
            {{x.value(), y.value(), z.value()}, std::move(delta).value()});
    }

    auto build_count = read_count<build::BuildPieceRecord>(reader, "build_pieces");
    if (!build_count) {
        return core::Result<SaveSnapshot>::failure(build_count.error().code,
                                                   build_count.error().message);
    }
    snapshot.build_pieces.reserve(build_count.value());
    for (std::uint32_t index = 0; index < build_count.value(); ++index) {
        auto id = reader.read_u64("build_id");
        auto prototype = read_prototype_id(reader, "build_prototype");
        auto transform = read_transform(reader);
        auto state = read_construction_state(reader);
        auto sockets = read_build_sockets(reader);
        auto ports = read_build_ports(reader);
        auto materials = read_string_list(reader, "material_tags");
        auto room_tags = read_string_list(reader, "room_tags");
        if (!id || !prototype || !transform || !state || !sockets || !ports || !materials ||
            !room_tags) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_build",
                                                       "binary save has invalid build record");
        }
        build::BuildPieceRecord record;
        record.object_id = core::SaveId::from_value(id.value());
        record.prototype_id = prototype.value();
        record.transform = transform.value();
        record.construction_state = state.value();
        record.sockets = std::move(sockets).value();
        record.network_ports = std::move(ports).value();
        record.material_tags = std::move(materials).value();
        record.room_contribution_tags = std::move(room_tags).value();
        snapshot.build_pieces.push_back(std::move(record));
    }

    auto entity_count = read_count<EntitySaveRecord>(reader, "entities");
    if (!entity_count) {
        return core::Result<SaveSnapshot>::failure(entity_count.error().code,
                                                   entity_count.error().message);
    }
    snapshot.entities.reserve(entity_count.value());
    for (std::uint32_t index = 0; index < entity_count.value(); ++index) {
        auto id = reader.read_u64("entity_id");
        auto prototype = read_prototype_id(reader, "entity_prototype");
        auto kind = read_entity_kind(reader);
        build::Transform transform;
        if (version.value() >= 3) {
            auto parsed_transform = read_transform(reader);
            if (!parsed_transform) {
                return core::Result<SaveSnapshot>::failure(parsed_transform.error().code,
                                                           parsed_transform.error().message);
            }
            transform = parsed_transform.value();
        }
        auto sleeping = reader.read_bool("entity_sleeping");
        auto state = reader.read_string("entity_state");
        if (!id || !prototype || !kind || !sleeping || !state) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_entity",
                                                       "binary save has invalid entity record");
        }
        snapshot.entities.push_back({core::SaveId::from_value(id.value()), prototype.value(),
                                     kind.value(), sleeping.value(), std::move(state).value(),
                                     transform});
    }

    auto inventory_count = read_count<InventorySaveRecord>(reader, "inventories");
    if (!inventory_count) {
        return core::Result<SaveSnapshot>::failure(inventory_count.error().code,
                                                   inventory_count.error().message);
    }
    snapshot.inventories.reserve(inventory_count.value());
    for (std::uint32_t index = 0; index < inventory_count.value(); ++index) {
        auto owner = reader.read_u64("inventory_owner");
        auto stacks = read_item_stacks(reader);
        if (!owner || !stacks) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_inventory",
                                                       "binary save has invalid inventory record");
        }
        snapshot.inventories.push_back(
            {core::SaveId::from_value(owner.value()), std::move(stacks).value()});
    }

    auto cargo_count = read_count<cargo::CargoRecord>(reader, "cargo_records");
    if (!cargo_count) {
        return core::Result<SaveSnapshot>::failure(cargo_count.error().code,
                                                   cargo_count.error().message);
    }
    snapshot.cargo_records.reserve(cargo_count.value());
    for (std::uint32_t index = 0; index < cargo_count.value(); ++index) {
        cargo::CargoRecord record;
        auto id = reader.read_u64("cargo_id");
        auto prototype = read_prototype_id(reader, "cargo_prototype");
        math::Vec3d position;
        if (version.value() >= 4) {
            auto parsed_position = read_vec3(reader, "cargo_position");
            if (!parsed_position) {
                return core::Result<SaveSnapshot>::failure(parsed_position.error().code,
                                                           parsed_position.error().message);
            }
            position = parsed_position.value();
        }
        auto mass = reader.read_u64("cargo_mass");
        auto volume = reader.read_u64("cargo_volume");
        auto stability = reader.read_i32("cargo_stability");
        auto transport = reader.read_u32("cargo_transport");
        auto hazards = read_string_list(reader, "cargo_hazards");
        if (!id || !prototype || !mass || !volume || !stability || !transport || !hazards) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_cargo",
                                                       "binary save has invalid cargo record");
        }
        record.cargo_id = core::SaveId::from_value(id.value());
        record.prototype_id = prototype.value();
        record.position = position;
        record.mass_grams = mass.value();
        record.volume_milliliters = volume.value();
        record.stability_per_mille = stability.value();
        record.allowed_transport_modes = cargo::CargoTransportModes::from_bits(transport.value());
        record.hazard_tags = std::move(hazards).value();
        snapshot.cargo_records.push_back(std::move(record));
    }

    auto workpiece_count = read_count<WorkpieceSaveRecord>(reader, "workpieces");
    if (!workpiece_count) {
        return core::Result<SaveSnapshot>::failure(workpiece_count.error().code,
                                                   workpiece_count.error().message);
    }
    snapshot.workpieces.reserve(workpiece_count.value());
    for (std::uint32_t index = 0; index < workpiece_count.value(); ++index) {
        auto id = reader.read_u64("workpiece_id");
        auto prototype = read_prototype_id(reader, "workpiece_prototype");
        auto width = reader.read_u16("workpiece_width");
        auto height = reader.read_u16("workpiece_height");
        auto depth = reader.read_u16("workpiece_depth");
        auto cells = reader.read_string("workpiece_cells");
        if (!id || !prototype || !width || !height || !depth || !cells) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_workpiece",
                                                       "binary save has invalid workpiece record");
        }
        snapshot.workpieces.push_back({core::WorkpieceId::from_value(id.value()),
                                       prototype.value(),
                                       {width.value(), height.value(), depth.value()},
                                       std::move(cells).value()});
    }

    auto assembly_count = read_count<assemblies::AssemblyRecord>(reader, "assemblies");
    if (!assembly_count) {
        return core::Result<SaveSnapshot>::failure(assembly_count.error().code,
                                                   assembly_count.error().message);
    }
    snapshot.assemblies.reserve(assembly_count.value());
    for (std::uint32_t index = 0; index < assembly_count.value(); ++index) {
        auto id = reader.read_u64("assembly_id");
        auto root = reader.read_u64("assembly_root");
        auto prototype = read_prototype_id(reader, "assembly_prototype");
        auto operating = reader.read_bool("assembly_operating");
        auto parts = read_assembly_parts(reader);
        auto ports = read_assembly_ports(reader);
        if (!id || !root || !prototype || !operating || !parts || !ports) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_assembly",
                                                       "binary save has invalid assembly record");
        }
        snapshot.assemblies.push_back({core::SaveId::from_value(id.value()),
                                       core::SaveId::from_value(root.value()), prototype.value(),
                                       std::move(parts).value(), std::move(ports).value(),
                                       operating.value()});
    }

    auto process_count = read_count<processes::ProcessInstance>(reader, "processes");
    if (!process_count) {
        return core::Result<SaveSnapshot>::failure(process_count.error().code,
                                                   process_count.error().message);
    }
    snapshot.processes.reserve(process_count.value());
    for (std::uint32_t index = 0; index < process_count.value(); ++index) {
        processes::ProcessInstance process;
        auto id = reader.read_u64("process_id");
        auto owner = reader.read_u64("process_owner");
        auto prototype = read_prototype_id(reader, "process_prototype");
        auto start = reader.read_i64("process_start");
        auto last = reader.read_i64("process_last");
        auto required = reader.read_i64("process_required");
        auto accumulated = reader.read_i64("process_accumulated");
        auto state = read_process_state(reader);
        auto interruption = reader.read_string("process_interruption");
        auto inputs = read_process_slots(reader);
        auto outputs = read_process_slots(reader);
        if (!id || !owner || !prototype || !start || !last || !required || !accumulated || !state ||
            !interruption || !inputs || !outputs) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_process",
                                                       "binary save has invalid process record");
        }
        process.process_id = core::ProcessId::from_value(id.value());
        process.owner_id = core::SaveId::from_value(owner.value());
        process.prototype_id = prototype.value();
        process.start_time_ms = start.value();
        process.last_update_time_ms = last.value();
        process.required_effective_work_ms = required.value();
        process.accumulated_effective_work_ms = accumulated.value();
        process.state = state.value();
        process.interruption_reason = std::move(interruption).value();
        process.input_slots = std::move(inputs).value();
        process.output_slots = std::move(outputs).value();
        snapshot.processes.push_back(std::move(process));
    }

    auto mod_state_count = read_count<ModStateSaveRecord>(reader, "mod_states");
    if (!mod_state_count) {
        return core::Result<SaveSnapshot>::failure(mod_state_count.error().code,
                                                   mod_state_count.error().message);
    }
    snapshot.mod_states.reserve(mod_state_count.value());
    for (std::uint32_t index = 0; index < mod_state_count.value(); ++index) {
        auto mod_id = reader.read_string("mod_state_id");
        auto key = reader.read_string("mod_state_key");
        auto state = reader.read_string("mod_state_payload");
        if (!mod_id || !key || !state) {
            return core::Result<SaveSnapshot>::failure("save_binary.invalid_mod_state",
                                                       "binary save has invalid mod state record");
        }
        snapshot.mod_states.push_back(
            {std::move(mod_id).value(), std::move(key).value(), std::move(state).value()});
    }

    if (!reader.eof()) {
        return core::Result<SaveSnapshot>::failure("save_binary.trailing_data",
                                                   "binary save has unexpected trailing bytes");
    }
    return core::Result<SaveSnapshot>::success(std::move(snapshot));
}

} // namespace heartstead::save
