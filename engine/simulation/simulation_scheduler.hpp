#pragma once

#include "engine/core/result.hpp"
#include "engine/simulation/tick_events.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::physics {
class IPhysicsWorld;
}

namespace heartstead::world {
class WorldState;
}

namespace heartstead::simulation {

enum class SimulationPhase : std::uint8_t {
    commands,
    movement,
    physics,
    world_operations,
    gameplay,
    environment,
    derived_state,
    finalize,
    replication,
};

struct SimulationContext {
    std::uint64_t tick = 0;
    double fixed_delta_seconds = 0.0;
    world::WorldState* world = nullptr;
    physics::IPhysicsWorld* physics = nullptr;
    TickEvents* events = nullptr;
};

using SimulationSystemCallback = std::function<core::Status(SimulationContext&)>;

struct SimulationSystemDesc {
    std::string name;
    SimulationPhase phase = SimulationPhase::gameplay;
    std::vector<std::string> after;
    SimulationSystemCallback update;
};

struct SimulationSystemTiming {
    std::string name;
    SimulationPhase phase = SimulationPhase::gameplay;
    double last_ms = 0.0;
    double total_ms = 0.0;
    std::uint64_t invocation_count = 0;
};

struct SimulationTickStats {
    std::uint64_t tick = 0;
    double total_ms = 0.0;
    std::uint32_t system_count = 0;
    std::uint32_t event_count = 0;
};

class SimulationScheduler {
  public:
    [[nodiscard]] core::Status register_system(SimulationSystemDesc desc);
    [[nodiscard]] core::Status finalize();
    [[nodiscard]] core::Result<SimulationTickStats> run_tick(SimulationContext context);

    [[nodiscard]] bool is_finalized() const noexcept;
    [[nodiscard]] std::span<const SimulationSystemTiming> timings() const noexcept;
    [[nodiscard]] std::vector<std::string_view> ordered_system_names() const;

  private:
    struct RegisteredSystem {
        SimulationSystemDesc desc;
        std::size_t registration_index = 0;
    };

    std::vector<RegisteredSystem> systems_;
    std::vector<std::size_t> execution_order_;
    std::vector<SimulationSystemTiming> timings_;
    bool finalized_ = false;
};

[[nodiscard]] std::string_view simulation_phase_name(SimulationPhase phase) noexcept;

} // namespace heartstead::simulation
