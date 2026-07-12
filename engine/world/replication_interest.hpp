#pragma once

#include "engine/core/result.hpp"
#include "engine/net/replication.hpp"
#include "engine/simulation/simulation_lod.hpp"
#include "engine/world/simulation_subjects.hpp"

#include <cstdint>
#include <vector>

namespace heartstead::net {
class HostSession;
} // namespace heartstead::net

namespace heartstead::world {

class WorldState;

struct WorldReplicationInterestOptions {
    WorldSimulationSubjectOptions subject_options;
    std::vector<simulation::SimulationViewer> viewers;
    simulation::SimulationLodPolicy policy;
    simulation::WorldTick now_ms = 0;
    bool broadcast_by_default = false;
    bool receives_global_events = true;
    bool include_full = true;
    bool include_simplified = true;
    bool include_sleeping = true;
    bool include_unloaded = false;
};

struct WorldReplicationViewerInterestReport {
    core::NetId viewer_id;
    std::uint32_t visible_subject_count = 0;
    std::uint32_t classified_full_count = 0;
    std::uint32_t classified_simplified_count = 0;
    std::uint32_t classified_sleeping_count = 0;
    std::uint32_t classified_unloaded_count = 0;
    std::uint32_t excluded_lod_subject_count = 0;
    std::uint32_t skipped_non_saved_subject_count = 0;
    std::vector<core::SaveId> visible_subjects;
};

struct WorldReplicationInterestReport {
    std::uint32_t subject_count = 0;
    std::uint32_t saved_subject_count = 0;
    std::uint32_t non_saved_subject_count = 0;
    std::uint32_t viewer_count = 0;
    bool broadcast_by_default = false;
    bool receives_global_events = true;
    bool include_full = true;
    bool include_simplified = true;
    bool include_sleeping = true;
    bool include_unloaded = false;
    net::ReplicationRelevancePolicy policy;
    std::vector<WorldReplicationViewerInterestReport> viewer_reports;
};

[[nodiscard]] core::Result<WorldReplicationInterestReport>
derive_replication_interest_report(const WorldState& state,
                                   const WorldReplicationInterestOptions& options);

[[nodiscard]] core::Result<net::ReplicationRelevancePolicy>
derive_replication_relevance_policy(const WorldState& state,
                                    const WorldReplicationInterestOptions& options);

[[nodiscard]] core::Result<WorldReplicationInterestReport>
refresh_host_session_replication_interest(net::HostSession& host, const WorldState& state,
                                          const WorldReplicationInterestOptions& options);

} // namespace heartstead::world
