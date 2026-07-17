#include "engine/simulation/simulation_scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace heartstead::simulation {

namespace {

[[nodiscard]] constexpr std::uint8_t phase_index(SimulationPhase phase) noexcept {
    return static_cast<std::uint8_t>(phase);
}

} // namespace

core::Status SimulationScheduler::register_system(SimulationSystemDesc desc) {
    if (finalized_) {
        return core::Status::failure("simulation_scheduler.already_finalized",
                                     "systems cannot be registered after scheduler finalization");
    }
    if (desc.name.empty() || !desc.update) {
        return core::Status::failure("simulation_scheduler.invalid_system",
                                     "a simulation system needs a name and update callback");
    }
    if (std::ranges::any_of(systems_, [&desc](const RegisteredSystem& system) {
            return system.desc.name == desc.name;
        })) {
        return core::Status::failure("simulation_scheduler.duplicate_system",
                                     "simulation system names must be unique: " + desc.name);
    }
    systems_.push_back({std::move(desc), systems_.size()});
    return core::Status::ok();
}

core::Status SimulationScheduler::finalize() {
    if (finalized_) {
        return core::Status::ok();
    }

    std::unordered_map<std::string, std::size_t> index_by_name;
    index_by_name.reserve(systems_.size());
    for (std::size_t index = 0; index < systems_.size(); ++index) {
        index_by_name.emplace(systems_[index].desc.name, index);
    }

    execution_order_.clear();
    execution_order_.reserve(systems_.size());
    constexpr auto phase_count = static_cast<std::uint8_t>(SimulationPhase::replication) + 1U;
    for (std::uint8_t raw_phase = 0; raw_phase < phase_count; ++raw_phase) {
        const auto phase = static_cast<SimulationPhase>(raw_phase);
        std::vector<std::size_t> pending;
        for (std::size_t index = 0; index < systems_.size(); ++index) {
            if (systems_[index].desc.phase == phase) {
                pending.push_back(index);
            }
        }

        while (!pending.empty()) {
            auto ready = pending.end();
            for (auto candidate = pending.begin(); candidate != pending.end(); ++candidate) {
                bool dependencies_ready = true;
                for (const auto& dependency_name : systems_[*candidate].desc.after) {
                    const auto dependency = index_by_name.find(dependency_name);
                    if (dependency == index_by_name.end()) {
                        return core::Status::failure(
                            "simulation_scheduler.unknown_dependency",
                            "simulation system dependency is not registered: " + dependency_name);
                    }
                    const auto dependency_phase = systems_[dependency->second].desc.phase;
                    if (phase_index(dependency_phase) > raw_phase) {
                        return core::Status::failure(
                            "simulation_scheduler.invalid_dependency_order",
                            "simulation system cannot depend on a later phase: " +
                                dependency_name);
                    }
                    if (dependency_phase == phase &&
                        std::ranges::find(execution_order_, dependency->second) ==
                            execution_order_.end()) {
                        dependencies_ready = false;
                        break;
                    }
                }
                if (dependencies_ready) {
                    ready = candidate;
                    break;
                }
            }
            if (ready == pending.end()) {
                return core::Status::failure(
                    "simulation_scheduler.dependency_cycle",
                    "simulation systems contain a same-phase dependency cycle");
            }
            execution_order_.push_back(*ready);
            pending.erase(ready);
        }
    }

    timings_.clear();
    timings_.reserve(execution_order_.size());
    for (const auto index : execution_order_) {
        timings_.push_back({systems_[index].desc.name, systems_[index].desc.phase});
    }
    finalized_ = true;
    return core::Status::ok();
}

core::Result<SimulationTickStats> SimulationScheduler::run_tick(SimulationContext context) {
    if (!finalized_) {
        return core::Result<SimulationTickStats>::failure(
            "simulation_scheduler.not_finalized",
            "simulation scheduler must be finalized before running ticks");
    }
    if (context.tick == 0 || !std::isfinite(context.fixed_delta_seconds) ||
        context.fixed_delta_seconds <= 0.0 || context.events == nullptr) {
        return core::Result<SimulationTickStats>::failure(
            "simulation_scheduler.invalid_context",
            "simulation tick needs a positive tick, delta, and event stream owner");
    }

    auto status = context.events->begin_tick(context.tick);
    if (!status) {
        return core::Result<SimulationTickStats>::failure(status.error().code,
                                                          status.error().message);
    }

    using Clock = std::chrono::steady_clock;
    const auto tick_start = Clock::now();
    for (std::size_t order = 0; order < execution_order_.size(); ++order) {
        const auto start = Clock::now();
        status = systems_[execution_order_[order]].desc.update(context);
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        auto& timing = timings_[order];
        timing.last_ms = elapsed;
        timing.total_ms += elapsed;
        ++timing.invocation_count;
        if (!status) {
            (void)context.events->seal();
            return core::Result<SimulationTickStats>::failure(
                "simulation_scheduler.system_failed",
                systems_[execution_order_[order]].desc.name + ": " + status.error().message);
        }
    }
    status = context.events->seal();
    if (!status) {
        return core::Result<SimulationTickStats>::failure(status.error().code,
                                                          status.error().message);
    }

    SimulationTickStats stats;
    stats.tick = context.tick;
    stats.total_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - tick_start).count();
    stats.system_count = static_cast<std::uint32_t>(execution_order_.size());
    stats.event_count = static_cast<std::uint32_t>(context.events->event_count());
    return core::Result<SimulationTickStats>::success(stats);
}

bool SimulationScheduler::is_finalized() const noexcept {
    return finalized_;
}

std::size_t SimulationScheduler::registered_system_count() const noexcept {
    return systems_.size();
}

std::span<const SimulationSystemTiming> SimulationScheduler::timings() const noexcept {
    return timings_;
}

std::vector<std::string_view> SimulationScheduler::ordered_system_names() const {
    std::vector<std::string_view> result;
    result.reserve(execution_order_.size());
    for (const auto index : execution_order_) {
        result.push_back(systems_[index].desc.name);
    }
    return result;
}

std::string_view simulation_phase_name(SimulationPhase phase) noexcept {
    switch (phase) {
    case SimulationPhase::commands:
        return "commands";
    case SimulationPhase::movement:
        return "movement";
    case SimulationPhase::physics:
        return "physics";
    case SimulationPhase::world_operations:
        return "world_operations";
    case SimulationPhase::gameplay:
        return "gameplay";
    case SimulationPhase::environment:
        return "environment";
    case SimulationPhase::derived_state:
        return "derived_state";
    case SimulationPhase::finalize:
        return "finalize";
    case SimulationPhase::replication:
        return "replication";
    }
    return "unknown";
}

} // namespace heartstead::simulation
