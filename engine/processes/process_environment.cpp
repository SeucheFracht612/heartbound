#include "engine/processes/process_environment.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

namespace heartstead::processes {

namespace {

[[nodiscard]] std::int64_t clamp_rate(std::int64_t rate) noexcept {
    return std::clamp(rate, std::int64_t{0}, std::int64_t{10000});
}

[[nodiscard]] bool has_source(const rooms::RoomRecord& room, core::SaveId owner_id) noexcept {
    return std::ranges::any_of(room.source_build_piece_ids,
                               [owner_id](core::SaveId id) { return id == owner_id; });
}

[[nodiscard]] bool has_descriptor(const rooms::RoomRecord& room, std::string_view code) noexcept {
    return room.has_descriptor(code);
}

void apply_descriptor_adjustment(const rooms::RoomRecord& room, std::string_view code,
                                 std::int64_t delta, std::int64_t& rate,
                                 std::vector<std::string>& factors) {
    if (!has_descriptor(room, code)) {
        return;
    }
    rate += delta;
    factors.push_back(std::string(code));
}

[[nodiscard]] std::int64_t room_rate_for(const rooms::RoomRecord& room,
                                         std::vector<std::string>& factors) {
    std::int64_t rate = 1000;

    apply_descriptor_adjustment(room, "enclosed", 100, rate, factors);
    apply_descriptor_adjustment(room, "exposed", -400, rate, factors);
    apply_descriptor_adjustment(room, "warm", 50, rate, factors);
    apply_descriptor_adjustment(room, "cold", -150, rate, factors);
    apply_descriptor_adjustment(room, "dry", 75, rate, factors);
    apply_descriptor_adjustment(room, "damp", -200, rate, factors);
    apply_descriptor_adjustment(room, "smoky", -300, rate, factors);
    apply_descriptor_adjustment(room, "ventilated", 75, rate, factors);
    apply_descriptor_adjustment(room, "unsafe", -250, rate, factors);
    apply_descriptor_adjustment(room, "crowded", -150, rate, factors);
    apply_descriptor_adjustment(room, "spacious", 75, rate, factors);
    apply_descriptor_adjustment(room, "storage_access", 50, rate, factors);
    apply_descriptor_adjustment(room, "cart_access", 25, rate, factors);
    apply_descriptor_adjustment(room, "poor_cart_access", -75, rate, factors);
    apply_descriptor_adjustment(room, "power_access", 75, rate, factors);
    apply_descriptor_adjustment(room, "warded", 25, rate, factors);

    return clamp_rate(rate);
}

[[nodiscard]] std::int64_t power_rate_for(std::uint32_t available_capacity,
                                          std::uint32_t required_capacity, bool requires_power,
                                          ProcessEnvironmentReport& report) {
    if (!requires_power) {
        if (available_capacity == 0) {
            return 1000;
        }
        report.factors.push_back("optional_power_access");
        return clamp_rate(1000 + std::min<std::int64_t>(500, available_capacity * 50LL));
    }

    report.power_satisfied = available_capacity >= required_capacity;
    if (available_capacity == 0) {
        report.warnings.push_back("missing_power");
        return 0;
    }

    if (required_capacity == 0) {
        return 1000;
    }

    const auto rate = (static_cast<std::int64_t>(available_capacity) * 1000LL) / required_capacity;
    if (!report.power_satisfied) {
        report.warnings.push_back("insufficient_power");
    } else {
        report.factors.push_back("power_satisfied");
    }
    return clamp_rate(rate);
}

} // namespace

const rooms::RoomRecord*
ProcessEnvironmentResolver::find_room_for_owner(const rooms::RoomGraph& graph,
                                                core::SaveId owner_id) noexcept {
    if (!owner_id.is_valid()) {
        return nullptr;
    }

    for (const auto* room : graph.rooms()) {
        if (room != nullptr && has_source(*room, owner_id)) {
            return room;
        }
    }
    return nullptr;
}

core::Result<ProcessEnvironmentReport>
ProcessEnvironmentResolver::resolve(const ProcessEnvironmentDesc& desc) {
    if (!desc.owner_id.is_valid()) {
        return core::Result<ProcessEnvironmentReport>::failure(
            "process_environment.invalid_owner", "process environment owner id must be valid");
    }
    if (desc.requires_power && desc.required_power_capacity == 0) {
        return core::Result<ProcessEnvironmentReport>::failure(
            "process_environment.invalid_power_requirement",
            "required power capacity must be non-zero when power is required");
    }

    ProcessEnvironmentReport report;
    const rooms::RoomRecord* room = nullptr;
    if (desc.room_graph != nullptr) {
        room = find_room_for_owner(*desc.room_graph, desc.owner_id);
    }

    report.room_found = room != nullptr;
    if (room != nullptr) {
        report.room_id = room->id;
        report.modifiers.room_rate_per_mille = room_rate_for(*room, report.factors);
    } else if (desc.requires_room) {
        report.modifiers.room_rate_per_mille = 0;
        report.warnings.push_back("missing_room");
    }

    report.modifiers.power_rate_per_mille = power_rate_for(
        desc.available_power_capacity, desc.required_power_capacity, desc.requires_power, report);
    report.modifiers.quality_rate_per_mille = clamp_rate(desc.base_quality_rate_per_mille);
    if (report.modifiers.quality_rate_per_mille != desc.base_quality_rate_per_mille) {
        report.warnings.push_back("quality_rate_clamped");
    }

    return core::Result<ProcessEnvironmentReport>::success(std::move(report));
}

} // namespace heartstead::processes
