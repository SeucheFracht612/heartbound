#include "engine/renderer/chunks/chunk_mesh_scheduler.hpp"

#include "engine/world/meshing/greedy_chunk_mesher.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace heartstead::renderer {

struct ChunkMeshScheduler::SharedState {
    explicit SharedState(std::size_t maximum_cached_buffers)
        : max_cached_buffers(maximum_cached_buffers) {}

    [[nodiscard]] std::vector<world::VoxelCell> acquire_cells(std::size_t minimum_capacity) {
        std::lock_guard lock(pool_mutex);
        auto best = cell_pool.end();
        for (auto candidate = cell_pool.begin(); candidate != cell_pool.end(); ++candidate) {
            if (candidate->capacity() >= minimum_capacity &&
                (best == cell_pool.end() || candidate->capacity() < best->capacity())) {
                best = candidate;
            }
        }
        if (best != cell_pool.end()) {
            auto result = std::move(*best);
            cell_pool.erase(best);
            return result;
        }
        std::vector<world::VoxelCell> result;
        result.reserve(minimum_capacity);
        return result;
    }

    void release_cells(std::vector<world::VoxelCell> cells) {
        cells.clear();
        std::lock_guard lock(pool_mutex);
        if (cell_pool.size() < max_cached_buffers) {
            cell_pool.push_back(std::move(cells));
        }
    }

    void publish(ChunkMeshResult result) {
        std::lock_guard lock(mailbox_mutex);
        mailbox.push_back(std::move(result));
    }

    [[nodiscard]] std::vector<ChunkMeshResult> drain(std::size_t maximum_results) {
        std::vector<ChunkMeshResult> result;
        std::lock_guard lock(mailbox_mutex);
        const auto count = std::min(maximum_results, mailbox.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            result.push_back(std::move(mailbox.front()));
            mailbox.pop_front();
        }
        return result;
    }

    [[nodiscard]] std::size_t mailbox_size() const noexcept {
        std::lock_guard lock(mailbox_mutex);
        return mailbox.size();
    }

    [[nodiscard]] std::pair<std::size_t, std::size_t> pool_stats() const noexcept {
        std::lock_guard lock(pool_mutex);
        std::size_t capacity = 0;
        for (const auto& cells : cell_pool) {
            capacity += cells.capacity();
        }
        return {cell_pool.size(), capacity};
    }

    std::size_t max_cached_buffers = 0;
    mutable std::mutex pool_mutex;
    std::vector<std::vector<world::VoxelCell>> cell_pool;
    mutable std::mutex mailbox_mutex;
    std::deque<ChunkMeshResult> mailbox;
};

namespace {

[[nodiscard]] jobs::JobPriority job_priority(const MeshPriority& priority) noexcept {
    if (priority.visible && priority.missing_resident_mesh) {
        return jobs::JobPriority::high;
    }
    if (priority.visible || priority.missing_resident_mesh) {
        return jobs::JobPriority::normal;
    }
    return jobs::JobPriority::low;
}

} // namespace

core::Status ChunkMeshSchedulerConfig::validate() const {
    if (worker_count == 0 || max_concurrent_jobs == 0 || max_completed_results == 0 ||
        max_cached_snapshot_buffers == 0) {
        return core::Status::failure(
            "renderer.invalid_chunk_mesh_scheduler_config",
            "chunk mesh scheduler limits and worker count must be nonzero");
    }
    if (max_concurrent_jobs < worker_count) {
        return core::Status::failure(
            "renderer.invalid_chunk_mesh_concurrency",
            "chunk mesh concurrent-job limit must be at least the worker count");
    }
    return core::Status::ok();
}

core::Result<std::unique_ptr<ChunkMeshScheduler>>
ChunkMeshScheduler::create(ChunkMeshSchedulerConfig config) {
    auto status = config.validate();
    if (!status) {
        return core::Result<std::unique_ptr<ChunkMeshScheduler>>::failure(status.error().code,
                                                                          status.error().message);
    }
    jobs::JobSystemDesc job_desc;
    job_desc.backend = jobs::JobBackend::thread_pool;
    job_desc.worker_count = config.worker_count;
    job_desc.max_completed_results = static_cast<std::uint32_t>(
        std::min(config.max_completed_results,
                 static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    auto job_system = jobs::create_job_system(job_desc);
    if (!job_system) {
        return core::Result<std::unique_ptr<ChunkMeshScheduler>>::failure(
            job_system.error().code, job_system.error().message);
    }
    auto shared_state = std::make_shared<SharedState>(config.max_cached_snapshot_buffers);
    return core::Result<std::unique_ptr<ChunkMeshScheduler>>::success(
        std::unique_ptr<ChunkMeshScheduler>(new ChunkMeshScheduler(
            config, std::move(job_system).value(), std::move(shared_state))));
}

ChunkMeshScheduler::ChunkMeshScheduler(ChunkMeshSchedulerConfig config,
                                       std::unique_ptr<jobs::IJobSystem> jobs,
                                       std::shared_ptr<SharedState> shared_state)
    : config_(config), jobs_(std::move(jobs)), shared_state_(std::move(shared_state)) {}

ChunkMeshScheduler::~ChunkMeshScheduler() {
    shutdown();
}

std::vector<world::VoxelCell>
ChunkMeshScheduler::acquire_snapshot_cells(std::size_t minimum_capacity) {
    return shared_state_->acquire_cells(minimum_capacity);
}

core::Status ChunkMeshScheduler::submit(ChunkMeshRequest request) {
    if (jobs_ == nullptr) {
        shared_state_->release_cells(std::move(request.neighborhood.cells));
        return core::Status::failure("renderer.chunk_mesh_scheduler_stopped",
                                     "chunk mesh scheduler is stopped");
    }
    auto snapshot_status = request.neighborhood.validate();
    if (!snapshot_status) {
        shared_state_->release_cells(std::move(request.neighborhood.cells));
        return snapshot_status;
    }
    if (!request.identity.is_valid() || request.identity != request.neighborhood.center_identity ||
        request.center_revision == 0 ||
        request.center_revision != request.neighborhood.center_revision ||
        request.render_table == nullptr || request.block_render_table_revision == 0 ||
        request.block_render_table_revision != request.render_table->revision) {
        shared_state_->release_cells(std::move(request.neighborhood.cells));
        return core::Status::failure("renderer.invalid_chunk_mesh_request",
                                     "chunk mesh request metadata is inconsistent");
    }
    if (active_jobs_.contains(request.identity)) {
        shared_state_->release_cells(std::move(request.neighborhood.cells));
        return core::Status::failure("renderer.chunk_mesh_request_coalesced",
                                     "a chunk mesh job is already active for this identity");
    }
    if (!has_capacity()) {
        shared_state_->release_cells(std::move(request.neighborhood.cells));
        return core::Status::failure("renderer.chunk_mesh_scheduler_full",
                                     "chunk mesh scheduler reached its concurrency budget");
    }

    const auto identity = request.identity;
    const auto center_revision = request.center_revision;
    const auto table_revision = request.block_render_table_revision;
    const auto priority = request.priority;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto shared_state = shared_state_;
    jobs::JobDesc job;
    job.name = "chunk_mesh";
    job.priority = job_priority(priority);
    job.work = [request = std::move(request), cancellation,
                shared_state](const jobs::JobContext&) mutable {
        ChunkMeshResult result;
        result.identity = request.identity;
        result.center_revision = request.center_revision;
        result.block_render_table_revision = request.block_render_table_revision;
        result.priority = request.priority;
        if (cancellation->load(std::memory_order_acquire)) {
            result.state = ChunkMeshResultState::cancelled;
            result.dependency_revisions = std::move(request.neighborhood.dependencies);
            shared_state->release_cells(std::move(request.neighborhood.cells));
            shared_state->publish(std::move(result));
            return core::Status::ok();
        }

        const auto started = std::chrono::steady_clock::now();
        try {
            auto mesh = world::GreedyChunkMesher::build_surface_mesh(request.neighborhood,
                                                                     *request.render_table);
            result.meshing_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
            if (mesh) {
                result.state = ChunkMeshResultState::succeeded;
                result.mesh = std::move(mesh).value();
            } else {
                result.state = ChunkMeshResultState::failed;
                result.error_code = mesh.error().code;
                result.error_message = mesh.error().message;
            }
        } catch (const std::exception& exception) {
            result.state = ChunkMeshResultState::failed;
            result.error_code = "renderer.chunk_mesh_job_exception";
            result.error_message = exception.what();
        } catch (...) {
            result.state = ChunkMeshResultState::failed;
            result.error_code = "renderer.chunk_mesh_job_exception";
            result.error_message = "chunk mesh worker threw an unknown exception";
        }
        result.dependency_revisions = std::move(request.neighborhood.dependencies);
        shared_state->release_cells(std::move(request.neighborhood.cells));
        shared_state->publish(std::move(result));
        return core::Status::ok();
    };

    auto submitted = jobs_->submit(std::move(job));
    if (!submitted) {
        // The moved request is owned by the rejected JobDesc and releases normally here. It was
        // never handed to a worker, so no active record is installed.
        return core::Status::failure(submitted.error().code, submitted.error().message);
    }
    active_jobs_.emplace(identity, ActiveJob{submitted.value(), center_revision, table_revision,
                                             std::move(cancellation)});
    ++stats_.submitted_jobs;
    refresh_stats();
    return core::Status::ok();
}

std::vector<ChunkMeshResult> ChunkMeshScheduler::drain_completed(std::size_t maximum_results) {
    if (jobs_ != nullptr) {
        (void)jobs_->drain_completed();
    }
    auto results = shared_state_->drain(maximum_results);
    for (const auto& result : results) {
        const auto active = active_jobs_.find(result.identity);
        if (active != active_jobs_.end() &&
            active->second.center_revision == result.center_revision &&
            active->second.render_table_revision == result.block_render_table_revision) {
            active_jobs_.erase(active);
        }
        ++stats_.completed_jobs;
        if (result.state == ChunkMeshResultState::cancelled) {
            ++stats_.cancelled_jobs;
        } else if (result.state == ChunkMeshResultState::failed) {
            ++stats_.failed_jobs;
        }
    }
    refresh_stats();
    return results;
}

void ChunkMeshScheduler::cancel(world::ChunkIdentity identity) noexcept {
    const auto active = active_jobs_.find(identity);
    if (active != active_jobs_.end()) {
        active->second.cancellation->store(true, std::memory_order_release);
    }
}

void ChunkMeshScheduler::cancel_all() noexcept {
    for (auto& [_, active] : active_jobs_) {
        active.cancellation->store(true, std::memory_order_release);
    }
}

void ChunkMeshScheduler::shutdown() noexcept {
    if (jobs_ == nullptr) {
        return;
    }
    cancel_all();
    jobs_.reset();
    (void)shared_state_->drain(static_cast<std::size_t>(-1));
    active_jobs_.clear();
    refresh_stats();
}

bool ChunkMeshScheduler::has_in_flight(world::ChunkIdentity identity) const noexcept {
    return active_jobs_.contains(identity);
}

std::optional<std::uint64_t>
ChunkMeshScheduler::in_flight_revision(world::ChunkIdentity identity) const noexcept {
    const auto active = active_jobs_.find(identity);
    if (active == active_jobs_.end()) {
        return std::nullopt;
    }
    return active->second.center_revision;
}

bool ChunkMeshScheduler::has_capacity() const noexcept {
    return active_jobs_.size() < config_.max_concurrent_jobs;
}

const ChunkMeshSchedulerStats& ChunkMeshScheduler::stats() noexcept {
    refresh_stats();
    return stats_;
}

void ChunkMeshScheduler::refresh_stats() noexcept {
    stats_.in_flight_jobs = active_jobs_.size();
    stats_.completed_mailbox_count = shared_state_->mailbox_size();
    const auto [buffers, cells] = shared_state_->pool_stats();
    stats_.pooled_snapshot_buffers = buffers;
    stats_.pooled_snapshot_capacity_cells = cells;
}

} // namespace heartstead::renderer
