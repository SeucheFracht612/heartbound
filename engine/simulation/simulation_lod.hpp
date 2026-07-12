#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/simulation/world_time.hpp"
#include "engine/world/coords/world_coords.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace heartstead::simulation {

enum class SimulationLod {
    full,
    simplified,
    sleeping,
    unloaded,
};

enum class SimulationSubjectKind {
    entity,
    build_piece,
    assembly,
    process_owner,
    network,
    chunk_region,
    custom,
};

using SimulationCoord = world::BlockCoord;

struct SimulationViewer {
    core::NetId viewer_id;
    SimulationCoord coord;
};

struct SimulationSubject {
    core::SaveId save_id;
    core::RuntimeHandle runtime_handle;
    core::ProcessId process_id;
    core::PrototypeId prototype_id;
    SimulationSubjectKind kind = SimulationSubjectKind::custom;
    SimulationCoord coord;
    WorldTick last_update_time_ms = 0;
    bool persistent = true;
    bool sleeping = false;
    std::optional<SimulationLod> forced_lod;
    std::string label;
};

struct SimulationLodPolicy {
    std::uint32_t full_radius = 64;
    std::uint32_t simplified_radius = 256;
    WorldTick full_tick_interval_ms = 16;
    WorldTick simplified_tick_interval_ms = 1000;
    WorldTick sleeping_tick_interval_ms = 10000;

    [[nodiscard]] core::Status validate() const;
};

struct SimulationLodDecision {
    core::SaveId save_id;
    core::RuntimeHandle runtime_handle;
    core::ProcessId process_id;
    SimulationSubjectKind kind = SimulationSubjectKind::custom;
    SimulationLod lod = SimulationLod::unloaded;
    std::uint64_t nearest_viewer_distance_squared = 0;
    WorldTick elapsed_since_update_ms = 0;
    WorldTick offline_delta_ms = 0;
    bool due_for_tick = false;
};

struct SimulationFramePlan {
    std::vector<SimulationLodDecision> decisions;
    std::size_t full_count = 0;
    std::size_t simplified_count = 0;
    std::size_t sleeping_count = 0;
    std::size_t unloaded_count = 0;
    std::size_t due_tick_count = 0;

    [[nodiscard]] std::size_t count(SimulationLod lod) const noexcept;
};

[[nodiscard]] const char* to_string(SimulationLod lod) noexcept;
[[nodiscard]] const char* to_string(SimulationSubjectKind kind) noexcept;

class SimulationLodPlanner {
  public:
    [[nodiscard]] static core::Result<SimulationLodDecision>
    classify(const SimulationSubject& subject, const std::vector<SimulationViewer>& viewers,
             const SimulationLodPolicy& policy, WorldTick now_ms);

    [[nodiscard]] static core::Result<SimulationFramePlan>
    plan_frame(const std::vector<SimulationSubject>& subjects,
               const std::vector<SimulationViewer>& viewers, const SimulationLodPolicy& policy,
               WorldTick now_ms);
};

} // namespace heartstead::simulation
