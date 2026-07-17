#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/jobs/job_system.hpp"

#include <chrono>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] std::vector<heartstead::jobs::JobResult>
wait_for_results(heartstead::jobs::IJobSystem& system, std::size_t expected_count) {
    std::vector<heartstead::jobs::JobResult> results;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (results.size() < expected_count && std::chrono::steady_clock::now() < deadline) {
        auto drained = system.drain_completed();
        results.insert(results.end(), std::make_move_iterator(drained.begin()),
                       std::make_move_iterator(drained.end()));
        if (results.size() < expected_count) {
            std::this_thread::yield();
        }
    }
    return results;
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        core::log(core::LogLevel::info, "Heartstead jobs sandbox starting");

        const auto thread_pool_info = jobs::job_backend_info(jobs::JobBackend::thread_pool);
        core::log(core::LogLevel::info,
                  "Thread pool backend status: " + std::string(thread_pool_info.status));

        auto system =
            jobs::create_job_system(jobs::JobSystemDesc{jobs::JobBackend::thread_pool, 2, 8});
        if (!system) {
            core::log(core::LogLevel::error, system.error().message);
            return 1;
        }

        auto first = system.value()->submit(jobs::JobDesc{
            "chunk.mesh.rebuild",
            jobs::JobPriority::high,
            [](const jobs::JobContext& context) {
                if (!context.id.is_valid()) {
                    return core::Status::failure("jobs_sandbox.invalid_context",
                                                 "job context id is invalid");
                }
                return core::Status::ok();
            },
        });
        if (!first) {
            core::log(core::LogLevel::error, first.error().message);
            return 1;
        }

        auto second = system.value()->submit(jobs::JobDesc{
            "asset.cook.failed_preview",
            jobs::JobPriority::low,
            [](const jobs::JobContext&) {
                return core::Status::failure("jobs_sandbox.expected_failure",
                                             "sample job failed intentionally");
            },
        });
        if (!second) {
            core::log(core::LogLevel::error, second.error().message);
            return 1;
        }

        auto completed = wait_for_results(*system.value(), 2);
        if (completed.size() != 2) {
            core::log(core::LogLevel::error, "expected two completed jobs");
            return 1;
        }

        for (const auto& result : completed) {
            core::log(core::LogLevel::info, "Job " + result.name + " completed as " +
                                                std::string(jobs::job_state_name(result.state)));
        }

        core::log(core::LogLevel::info, "Jobs sandbox submitted " +
                                            std::to_string(system.value()->submitted_count()) +
                                            " jobs");
        return 0;
    });
}
