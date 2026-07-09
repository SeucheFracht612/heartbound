#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/processes/process.hpp"
#include "engine/rooms/room_graph.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::processes {

struct ProcessEnvironmentDesc {
    core::SaveId owner_id;
    const rooms::RoomGraph* room_graph = nullptr;
    bool requires_room = false;
    bool requires_power = false;
    std::uint32_t available_power_capacity = 0;
    std::uint32_t required_power_capacity = 1;
    std::int64_t base_quality_rate_per_mille = 1000;
};

struct ProcessEnvironmentReport {
    ProcessModifiers modifiers;
    rooms::RoomId room_id;
    bool room_found = false;
    bool power_satisfied = true;
    std::vector<std::string> factors;
    std::vector<std::string> warnings;
};

class ProcessEnvironmentResolver {
  public:
    [[nodiscard]] static const rooms::RoomRecord*
    find_room_for_owner(const rooms::RoomGraph& graph, core::SaveId owner_id) noexcept;

    [[nodiscard]] static core::Result<ProcessEnvironmentReport>
    resolve(const ProcessEnvironmentDesc& desc);
};

} // namespace heartstead::processes
