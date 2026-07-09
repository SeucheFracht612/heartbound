#include "engine/simulation/simulation_lod.hpp"

#include <algorithm>
#include <limits>

namespace heartstead::simulation {

namespace {

[[nodiscard]] std::uint64_t ordered_axis_bits(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value) ^ (std::uint64_t{1} << 63U);
}

[[nodiscard]] std::uint64_t abs_diff(std::int64_t a, std::int64_t b) noexcept {
    const auto left = ordered_axis_bits(a);
    const auto right = ordered_axis_bits(b);
    if (left >= right) {
        return left - right;
    }
    return right - left;
}

[[nodiscard]] std::uint64_t saturated_square(std::uint64_t value) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (value != 0 && value > max / value) {
        return max;
    }
    return value * value;
}

[[nodiscard]] std::uint64_t saturated_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max - right) {
        return max;
    }
    return left + right;
}

[[nodiscard]] std::uint64_t squared_distance(SimulationCoord a, SimulationCoord b) noexcept {
    const auto dx = saturated_square(abs_diff(a.x, b.x));
    const auto dy = saturated_square(abs_diff(a.y, b.y));
    const auto dz = saturated_square(abs_diff(a.z, b.z));
    return saturated_add(saturated_add(dx, dy), dz);
}

[[nodiscard]] std::uint64_t radius_squared(std::uint32_t radius) noexcept {
    return saturated_square(static_cast<std::uint64_t>(radius));
}

[[nodiscard]] std::uint64_t
nearest_distance_squared(SimulationCoord subject_coord,
                         const std::vector<SimulationViewer>& viewers) noexcept {
    if (viewers.empty()) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    auto nearest = std::numeric_limits<std::uint64_t>::max();
    for (const auto& viewer : viewers) {
        nearest = std::min(nearest, squared_distance(subject_coord, viewer.coord));
    }
    return nearest;
}

[[nodiscard]] core::Status validate_subject(const SimulationSubject& subject) {
    if (subject.persistent && !subject.save_id.is_valid()) {
        return core::Status::failure("simulation.missing_save_id",
                                     "persistent simulation subjects need stable save ids");
    }
    if (!subject.persistent && subject.save_id.is_valid()) {
        return core::Status::failure("simulation.unexpected_save_id",
                                     "non-persistent simulation subjects must not claim a save id");
    }
    if (subject.last_update_time_ms < 0) {
        return core::Status::failure("simulation.invalid_last_update_time",
                                     "simulation subject last update time must be non-negative");
    }
    if (subject.kind == SimulationSubjectKind::process_owner && !subject.process_id.is_valid()) {
        return core::Status::failure("simulation.missing_process_id",
                                     "process-owner simulation subjects need stable process ids");
    }
    return core::Status::ok();
}

[[nodiscard]] SimulationLod distance_lod(std::uint64_t nearest_squared,
                                         const SimulationLodPolicy& policy) noexcept {
    if (nearest_squared <= radius_squared(policy.full_radius)) {
        return SimulationLod::full;
    }
    if (nearest_squared <= radius_squared(policy.simplified_radius)) {
        return SimulationLod::simplified;
    }
    return SimulationLod::unloaded;
}

[[nodiscard]] std::int64_t tick_interval_for(SimulationLod lod,
                                             const SimulationLodPolicy& policy) noexcept {
    switch (lod) {
    case SimulationLod::full:
        return policy.full_tick_interval_ms;
    case SimulationLod::simplified:
        return policy.simplified_tick_interval_ms;
    case SimulationLod::sleeping:
        return policy.sleeping_tick_interval_ms;
    case SimulationLod::unloaded:
        return 0;
    }
    return 0;
}

void increment_lod_count(SimulationFramePlan& plan, SimulationLod lod) noexcept {
    switch (lod) {
    case SimulationLod::full:
        ++plan.full_count;
        break;
    case SimulationLod::simplified:
        ++plan.simplified_count;
        break;
    case SimulationLod::sleeping:
        ++plan.sleeping_count;
        break;
    case SimulationLod::unloaded:
        ++plan.unloaded_count;
        break;
    }
}

} // namespace

core::Status SimulationLodPolicy::validate() const {
    if (full_radius > simplified_radius) {
        return core::Status::failure("simulation.invalid_radius",
                                     "full simulation radius cannot exceed simplified radius");
    }
    if (full_tick_interval_ms <= 0 || simplified_tick_interval_ms <= 0 ||
        sleeping_tick_interval_ms <= 0) {
        return core::Status::failure("simulation.invalid_tick_interval",
                                     "simulation tick intervals must be positive");
    }
    return core::Status::ok();
}

std::size_t SimulationFramePlan::count(SimulationLod lod) const noexcept {
    switch (lod) {
    case SimulationLod::full:
        return full_count;
    case SimulationLod::simplified:
        return simplified_count;
    case SimulationLod::sleeping:
        return sleeping_count;
    case SimulationLod::unloaded:
        return unloaded_count;
    }
    return 0;
}

const char* to_string(SimulationLod lod) noexcept {
    switch (lod) {
    case SimulationLod::full:
        return "full";
    case SimulationLod::simplified:
        return "simplified";
    case SimulationLod::sleeping:
        return "sleeping";
    case SimulationLod::unloaded:
        return "unloaded";
    }
    return "unknown";
}

const char* to_string(SimulationSubjectKind kind) noexcept {
    switch (kind) {
    case SimulationSubjectKind::entity:
        return "entity";
    case SimulationSubjectKind::build_piece:
        return "build_piece";
    case SimulationSubjectKind::assembly:
        return "assembly";
    case SimulationSubjectKind::process_owner:
        return "process_owner";
    case SimulationSubjectKind::network:
        return "network";
    case SimulationSubjectKind::chunk_region:
        return "chunk_region";
    case SimulationSubjectKind::custom:
        return "custom";
    }
    return "unknown";
}

core::Result<SimulationLodDecision>
SimulationLodPlanner::classify(const SimulationSubject& subject,
                               const std::vector<SimulationViewer>& viewers,
                               const SimulationLodPolicy& policy, std::int64_t now_ms) {
    const auto policy_status = policy.validate();
    if (!policy_status) {
        return core::Result<SimulationLodDecision>::failure(policy_status.error().code,
                                                            policy_status.error().message);
    }

    const auto subject_status = validate_subject(subject);
    if (!subject_status) {
        return core::Result<SimulationLodDecision>::failure(subject_status.error().code,
                                                            subject_status.error().message);
    }

    if (now_ms < subject.last_update_time_ms) {
        return core::Result<SimulationLodDecision>::failure("simulation.time_reversed",
                                                            "simulation time cannot move backward");
    }

    SimulationLodDecision decision;
    decision.save_id = subject.save_id;
    decision.runtime_handle = subject.runtime_handle;
    decision.process_id = subject.process_id;
    decision.kind = subject.kind;
    decision.nearest_viewer_distance_squared = nearest_distance_squared(subject.coord, viewers);
    decision.elapsed_since_update_ms = now_ms - subject.last_update_time_ms;

    if (subject.forced_lod.has_value()) {
        decision.lod = subject.forced_lod.value();
    } else {
        decision.lod = distance_lod(decision.nearest_viewer_distance_squared, policy);
        if (subject.sleeping && decision.lod != SimulationLod::unloaded) {
            decision.lod = SimulationLod::sleeping;
        }
    }

    if (decision.lod == SimulationLod::unloaded) {
        decision.offline_delta_ms = decision.elapsed_since_update_ms;
        return core::Result<SimulationLodDecision>::success(decision);
    }

    const auto interval = tick_interval_for(decision.lod, policy);
    decision.due_for_tick = decision.elapsed_since_update_ms >= interval;
    return core::Result<SimulationLodDecision>::success(decision);
}

core::Result<SimulationFramePlan>
SimulationLodPlanner::plan_frame(const std::vector<SimulationSubject>& subjects,
                                 const std::vector<SimulationViewer>& viewers,
                                 const SimulationLodPolicy& policy, std::int64_t now_ms) {
    SimulationFramePlan plan;
    plan.decisions.reserve(subjects.size());

    for (const auto& subject : subjects) {
        auto decision = classify(subject, viewers, policy, now_ms);
        if (!decision) {
            return core::Result<SimulationFramePlan>::failure(decision.error().code,
                                                              decision.error().message);
        }

        increment_lod_count(plan, decision.value().lod);
        if (decision.value().due_for_tick) {
            ++plan.due_tick_count;
        }
        plan.decisions.push_back(std::move(decision).value());
    }

    return core::Result<SimulationFramePlan>::success(std::move(plan));
}

} // namespace heartstead::simulation
