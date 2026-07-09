#include "engine/world/replication_interest.hpp"

#include "engine/net/host_session.hpp"
#include "engine/world/world_state.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::world {

namespace {

[[nodiscard]] bool include_lod(simulation::SimulationLod lod,
                               const WorldReplicationInterestOptions& options) noexcept {
    switch (lod) {
    case simulation::SimulationLod::full:
        return options.include_full;
    case simulation::SimulationLod::simplified:
        return options.include_simplified;
    case simulation::SimulationLod::sleeping:
        return options.include_sleeping;
    case simulation::SimulationLod::unloaded:
        return options.include_unloaded;
    }
    return false;
}

[[nodiscard]] bool contains_id(const std::vector<core::SaveId>& ids, core::SaveId id) noexcept {
    return std::ranges::find(ids, id) != ids.end();
}

void count_lod(WorldReplicationViewerInterestReport& report,
               simulation::SimulationLod lod) noexcept {
    switch (lod) {
    case simulation::SimulationLod::full:
        ++report.classified_full_count;
        return;
    case simulation::SimulationLod::simplified:
        ++report.classified_simplified_count;
        return;
    case simulation::SimulationLod::sleeping:
        ++report.classified_sleeping_count;
        return;
    case simulation::SimulationLod::unloaded:
        ++report.classified_unloaded_count;
        return;
    }
}

void sort_unique_ids(std::vector<core::SaveId>& ids) {
    std::ranges::sort(ids,
                      [](core::SaveId lhs, core::SaveId rhs) { return lhs.value() < rhs.value(); });
    const auto last = std::ranges::unique(ids);
    ids.erase(last.begin(), last.end());
}

[[nodiscard]] bool has_viewer_id(const std::vector<simulation::SimulationViewer>& viewers,
                                 core::NetId viewer_id) noexcept {
    return std::ranges::find_if(viewers, [viewer_id](const simulation::SimulationViewer& viewer) {
               return viewer.viewer_id == viewer_id;
           }) != viewers.end();
}

[[nodiscard]] core::Status
validate_viewers(const std::vector<simulation::SimulationViewer>& viewers) {
    std::vector<simulation::SimulationViewer> seen;
    seen.reserve(viewers.size());
    for (const auto& viewer : viewers) {
        if (!viewer.viewer_id.is_valid()) {
            return core::Status::failure("replication_interest.invalid_viewer",
                                         "replication interest viewers need valid net ids");
        }
        if (has_viewer_id(seen, viewer.viewer_id)) {
            return core::Status::failure("replication_interest.duplicate_viewer",
                                         "replication interest viewers must be unique by net id");
        }
        seen.push_back(viewer);
    }
    return core::Status::ok();
}

} // namespace

core::Result<WorldReplicationInterestReport>
derive_replication_interest_report(const WorldState& state,
                                   const WorldReplicationInterestOptions& options) {
    auto viewer_status = validate_viewers(options.viewers);
    if (!viewer_status) {
        return core::Result<WorldReplicationInterestReport>::failure(viewer_status.error().code,
                                                                     viewer_status.error().message);
    }

    auto subjects = derive_simulation_subjects(state, options.subject_options);
    if (!subjects) {
        return core::Result<WorldReplicationInterestReport>::failure(subjects.error().code,
                                                                     subjects.error().message);
    }

    WorldReplicationInterestReport report;
    report.subject_count = static_cast<std::uint32_t>(subjects.value().size());
    report.viewer_count = static_cast<std::uint32_t>(options.viewers.size());
    report.broadcast_by_default = options.broadcast_by_default;
    report.receives_global_events = options.receives_global_events;
    report.include_full = options.include_full;
    report.include_simplified = options.include_simplified;
    report.include_sleeping = options.include_sleeping;
    report.include_unloaded = options.include_unloaded;
    report.policy.broadcast_by_default = options.broadcast_by_default;
    report.policy.client_rules.reserve(options.viewers.size());
    report.viewer_reports.reserve(options.viewers.size());

    for (const auto& subject : subjects.value()) {
        if (subject.save_id.is_valid()) {
            ++report.saved_subject_count;
        } else {
            ++report.non_saved_subject_count;
        }
    }

    for (const auto& viewer : options.viewers) {
        net::ReplicationInterestRule rule;
        rule.client_id = viewer.viewer_id;
        rule.receives_global_events = options.receives_global_events;

        WorldReplicationViewerInterestReport viewer_report;
        viewer_report.viewer_id = viewer.viewer_id;

        for (const auto& subject : subjects.value()) {
            if (!subject.save_id.is_valid()) {
                ++viewer_report.skipped_non_saved_subject_count;
                continue;
            }

            auto decision = simulation::SimulationLodPlanner::classify(
                subject, {viewer}, options.policy, options.now_ms);
            if (!decision) {
                return core::Result<WorldReplicationInterestReport>::failure(
                    decision.error().code, decision.error().message);
            }
            count_lod(viewer_report, decision.value().lod);
            if (include_lod(decision.value().lod, options)) {
                if (!contains_id(viewer_report.visible_subjects, subject.save_id)) {
                    viewer_report.visible_subjects.push_back(subject.save_id);
                }
            } else {
                ++viewer_report.excluded_lod_subject_count;
            }
        }

        sort_unique_ids(viewer_report.visible_subjects);
        viewer_report.visible_subject_count =
            static_cast<std::uint32_t>(viewer_report.visible_subjects.size());
        rule.visible_subjects = viewer_report.visible_subjects;
        report.viewer_reports.push_back(std::move(viewer_report));
        report.policy.client_rules.push_back(std::move(rule));
    }

    std::ranges::sort(report.policy.client_rules, [](const auto& lhs, const auto& rhs) {
        return lhs.client_id.value() < rhs.client_id.value();
    });
    std::ranges::sort(report.viewer_reports, [](const auto& lhs, const auto& rhs) {
        return lhs.viewer_id.value() < rhs.viewer_id.value();
    });
    return core::Result<WorldReplicationInterestReport>::success(std::move(report));
}

core::Result<net::ReplicationRelevancePolicy>
derive_replication_relevance_policy(const WorldState& state,
                                    const WorldReplicationInterestOptions& options) {
    auto report = derive_replication_interest_report(state, options);
    if (!report) {
        return core::Result<net::ReplicationRelevancePolicy>::failure(report.error().code,
                                                                      report.error().message);
    }
    return core::Result<net::ReplicationRelevancePolicy>::success(std::move(report).value().policy);
}

core::Result<WorldReplicationInterestReport>
refresh_host_session_replication_interest(net::HostSession& host, const WorldState& state,
                                          const WorldReplicationInterestOptions& options) {
    auto report = derive_replication_interest_report(state, options);
    if (!report) {
        return report;
    }

    auto policy = report.value().policy;
    host.set_replication_relevance_policy(std::move(policy));
    return core::Result<WorldReplicationInterestReport>::success(std::move(report).value());
}

} // namespace heartstead::world
