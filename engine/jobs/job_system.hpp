#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::jobs {

struct JobIdTag;
using JobId = core::StrongU64Id<JobIdTag>;

enum class JobBackend {
    immediate,
    thread_pool,
};

enum class JobPriority {
    low,
    normal,
    high,
};

enum class JobState {
    queued,
    running,
    succeeded,
    failed,
    cancelled,
};

struct JobBackendInfo {
    JobBackend backend = JobBackend::immediate;
    std::string_view name;
    bool available = false;
    std::string_view status;
};

struct JobContext {
    JobId id;
    std::string_view name;
    JobPriority priority = JobPriority::normal;
    bool cancellation_requested = false;
};

using JobFunction = std::function<core::Status(const JobContext&)>;

struct JobDesc {
    std::string name;
    JobPriority priority = JobPriority::normal;
    JobFunction work;
};

struct JobResult {
    JobId id;
    std::string name;
    JobPriority priority = JobPriority::normal;
    JobState state = JobState::queued;
    std::uint64_t completion_order = 0;
    std::string error_code;
    std::string error_message;
};

struct JobSystemDesc {
    JobBackend backend = JobBackend::immediate;
    std::uint32_t worker_count = 1;
    std::uint32_t max_completed_results = 1024;
};

class IJobSystem {
  public:
    virtual ~IJobSystem() = default;

    [[nodiscard]] virtual JobBackend backend() const noexcept = 0;
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t pending_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t submitted_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t completed_count() const noexcept = 0;

    [[nodiscard]] virtual core::Result<JobId> submit(JobDesc desc) = 0;
    [[nodiscard]] virtual std::vector<JobResult> drain_completed() = 0;
};

[[nodiscard]] core::Result<std::unique_ptr<IJobSystem>> create_job_system(JobSystemDesc desc);

[[nodiscard]] core::Status validate_job_system_desc(const JobSystemDesc& desc);
[[nodiscard]] core::Status validate_job_desc(const JobDesc& desc);

[[nodiscard]] JobBackendInfo job_backend_info(JobBackend backend) noexcept;
[[nodiscard]] std::string_view job_backend_name(JobBackend backend) noexcept;
[[nodiscard]] std::string_view job_priority_name(JobPriority priority) noexcept;
[[nodiscard]] std::string_view job_state_name(JobState state) noexcept;

} // namespace heartstead::jobs
