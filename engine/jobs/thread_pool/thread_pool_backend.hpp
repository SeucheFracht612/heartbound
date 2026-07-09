#pragma once

#include "engine/jobs/job_system.hpp"

namespace heartstead::jobs::thread_pool {

[[nodiscard]] JobBackendInfo backend_info() noexcept;

[[nodiscard]] core::Result<std::unique_ptr<IJobSystem>> create_job_system(JobSystemDesc desc);

} // namespace heartstead::jobs::thread_pool
