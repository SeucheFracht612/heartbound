#include "engine/renderer/assets/mesh_manager.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace heartstead::renderer {

struct MeshManager::Record {
    std::uint32_t generation = 1;
    bool occupied = false;
    std::string id;
    RenderMeshView view;
};

namespace {

constexpr std::array<GpuStaticMeshVertex, 24> fallback_vertices{{
    {{-0.5F, -0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
     {{0.5F, -0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
     {{0.5F, 0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
     {{-0.5F, 0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
     {{0.5F, -0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F}},
     {{-0.5F, -0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F}},
     {{-0.5F, 0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}},
     {{0.5F, 0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}},
     {{-0.5F, -0.5F, -0.5F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
     {{-0.5F, -0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
     {{-0.5F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
     {{-0.5F, 0.5F, -0.5F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
     {{0.5F, -0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
     {{0.5F, -0.5F, -0.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
     {{0.5F, 0.5F, -0.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
     {{0.5F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
     {{-0.5F, 0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
     {{0.5F, 0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
     {{0.5F, 0.5F, -0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
     {{-0.5F, 0.5F, -0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}},
     {{-0.5F, -0.5F, -0.5F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F}},
     {{0.5F, -0.5F, -0.5F}, {0.0F, -1.0F, 0.0F}, {1.0F, 0.0F}},
     {{0.5F, -0.5F, 0.5F}, {0.0F, -1.0F, 0.0F}, {1.0F, 1.0F}},
     {{-0.5F, -0.5F, 0.5F}, {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F}},
}};

constexpr std::array<std::uint32_t, 36> fallback_indices{
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
};

} // namespace

core::Status MeshManagerConfig::validate() const {
    GpuBufferArenaConfig vertices;
    vertices.usage = rhi::RenderBufferUsage::vertex;
    vertices.initial_block_bytes = vertex_initial_bytes;
    vertices.maximum_capacity_bytes = vertex_maximum_bytes;
    vertices.debug_name = "static_mesh_vertex_arena";
    auto status = vertices.validate();
    if (!status) {
        return status;
    }
    GpuBufferArenaConfig indices;
    indices.usage = rhi::RenderBufferUsage::index;
    indices.initial_block_bytes = index_initial_bytes;
    indices.maximum_capacity_bytes = index_maximum_bytes;
    indices.debug_name = "static_mesh_index_arena";
    return indices.validate();
}

core::Status validate_static_mesh_upload(const StaticMeshUploadDesc& desc) {
    if (desc.id.empty() || desc.vertices.empty() || desc.indices.empty() ||
        !desc.local_bounds.is_valid() || desc.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        desc.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return core::Status::failure("mesh_manager.invalid_mesh",
                                     "static mesh upload metadata or geometry is invalid");
    }
    for (const auto index : desc.indices) {
        if (index >= desc.vertices.size()) {
            return core::Status::failure("mesh_manager.index_out_of_bounds",
                                         "static mesh index references a missing vertex");
        }
    }
    for (const auto& vertex : desc.vertices) {
        for (const auto value : vertex.position) {
            if (!std::isfinite(value)) {
                return core::Status::failure("mesh_manager.non_finite_vertex",
                                             "static mesh position must be finite");
            }
        }
        for (const auto value : vertex.normal) {
            if (!std::isfinite(value)) {
                return core::Status::failure("mesh_manager.non_finite_vertex",
                                             "static mesh normal must be finite");
            }
        }
        for (const auto value : vertex.uv) {
            if (!std::isfinite(value)) {
                return core::Status::failure("mesh_manager.non_finite_vertex",
                                             "static mesh UV must be finite");
            }
        }
    }
    return core::Status::ok();
}

MeshManager::MeshManager(rhi::IRenderDevice& device) : device_(&device) {}

MeshManager::~MeshManager() {
    (void)shutdown();
}

core::Status MeshManager::initialize(MeshManagerConfig config) {
    if (vertex_arena_ != nullptr || index_arena_ != nullptr) {
        return core::Status::failure("mesh_manager.already_initialized",
                                     "mesh manager cannot be initialized twice");
    }
    auto status = config.validate();
    if (!status) {
        return status;
    }
    GpuBufferArenaConfig vertex_config;
    vertex_config.usage = rhi::RenderBufferUsage::vertex;
    vertex_config.initial_block_bytes = config.vertex_initial_bytes;
    vertex_config.maximum_capacity_bytes = config.vertex_maximum_bytes;
    vertex_config.debug_name = "static_mesh_vertex_arena";
    auto vertices = GpuBufferArena::create(*device_, std::move(vertex_config));
    if (!vertices) {
        return core::Status::failure(vertices.error().code, vertices.error().message);
    }
    GpuBufferArenaConfig index_config;
    index_config.usage = rhi::RenderBufferUsage::index;
    index_config.initial_block_bytes = config.index_initial_bytes;
    index_config.maximum_capacity_bytes = config.index_maximum_bytes;
    index_config.debug_name = "static_mesh_index_arena";
    auto indices = GpuBufferArena::create(*device_, std::move(index_config));
    if (!indices) {
        return core::Status::failure(indices.error().code, indices.error().message);
    }
    vertex_arena_ = std::move(vertices).value();
    index_arena_ = std::move(indices).value();
    auto fallback = upload_mesh({"__error_cube", fallback_vertices, fallback_indices,
                                 {{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}},
                                true);
    if (!fallback) {
        const auto error = fallback.error();
        (void)shutdown();
        return core::Status::failure(error.code, error.message);
    }
    fallback_mesh_ = fallback.value();
    return core::Status::ok();
}

core::Result<RenderMeshHandle> MeshManager::create_mesh(const StaticMeshUploadDesc& desc) {
    if (const auto* existing = find(desc.id); existing != nullptr) {
        return core::Result<RenderMeshHandle>::success(existing->handle);
    }
    return upload_mesh(desc, false);
}

core::Result<RenderMeshHandle> MeshManager::upload_mesh(const StaticMeshUploadDesc& desc,
                                                       bool fallback) {
    if (vertex_arena_ == nullptr || index_arena_ == nullptr) {
        return core::Result<RenderMeshHandle>::failure("mesh_manager.not_initialized",
                                                       "mesh manager must be initialized first");
    }
    auto status = validate_static_mesh_upload(desc);
    if (!status) {
        return core::Result<RenderMeshHandle>::failure(status.error().code, status.error().message);
    }
    const auto vertex_bytes = std::as_bytes(desc.vertices);
    const auto index_bytes = std::as_bytes(desc.indices);
    auto vertices = vertex_arena_->allocate(vertex_bytes.size(), alignof(GpuStaticMeshVertex));
    if (!vertices) {
        return core::Result<RenderMeshHandle>::failure(vertices.error().code,
                                                       vertices.error().message);
    }
    auto indices = index_arena_->allocate(index_bytes.size(), alignof(std::uint32_t));
    if (!indices) {
        (void)vertex_arena_->retire(vertices.value(), device_->completed_submission_serial());
        collect();
        return core::Result<RenderMeshHandle>::failure(indices.error().code, indices.error().message);
    }
    const std::array writes{
        rhi::RenderBufferWrite{vertices.value().buffer,
                               static_cast<std::size_t>(vertices.value().offset), vertex_bytes},
        rhi::RenderBufferWrite{indices.value().buffer,
                               static_cast<std::size_t>(indices.value().offset), index_bytes},
    };
    auto uploaded = device_->upload_buffer_batch(writes);
    if (!uploaded) {
        (void)vertex_arena_->retire(vertices.value(), device_->completed_submission_serial());
        (void)index_arena_->retire(indices.value(), device_->completed_submission_serial());
        collect();
        return core::Result<RenderMeshHandle>::failure(uploaded.error().code,
                                                       uploaded.error().message);
    }
    std::size_t slot_index = records_.size();
    for (std::size_t index = 0; index < records_.size(); ++index) {
        if (!records_[index].occupied) {
            slot_index = index;
            break;
        }
    }
    if (slot_index == records_.size()) {
        records_.emplace_back();
    }
    auto& record = records_[slot_index];
    record.occupied = true;
    record.id = desc.id;
    record.view = {{static_cast<std::uint32_t>(slot_index + 1U), record.generation},
                   record.id,
                   vertices.value(),
                   indices.value(),
                   static_cast<std::uint32_t>(desc.vertices.size()),
                   static_cast<std::uint32_t>(desc.indices.size()),
                   rhi::RenderIndexType::uint32,
                   desc.local_bounds,
                   fallback};
    ++stats_.uploaded_mesh_count;
    stats_.uploaded_bytes += vertex_bytes.size() + index_bytes.size();
    refresh_stats();
    return core::Result<RenderMeshHandle>::success(record.view.handle);
}

core::Status MeshManager::release(RenderMeshHandle handle) {
    auto* record = find_record(handle);
    if (record == nullptr) {
        return core::Status::failure("mesh_manager.stale_handle",
                                     "static mesh release uses a missing or stale handle");
    }
    if (handle == fallback_mesh_) {
        return core::Status::failure("mesh_manager.release_fallback",
                                     "the static mesh fallback is manager-owned");
    }
    return retire_record(*record);
}

core::Status MeshManager::retire_record(Record& record) {
    auto status = vertex_arena_->retire(record.view.vertices, device_->last_submission_serial());
    auto index_status = index_arena_->retire(record.view.indices, device_->last_submission_serial());
    record.occupied = false;
    record.id.clear();
    record.view = {};
    ++record.generation;
    if (record.generation == 0) {
        std::terminate();
    }
    collect();
    refresh_stats();
    return !status ? status : index_status;
}

const RenderMeshView* MeshManager::find(RenderMeshHandle handle) noexcept {
    if (const auto* exact = find_exact(handle); exact != nullptr) {
        return exact;
    }
    if (handle.is_valid()) {
        ++stats_.fallback_resolution_count;
    }
    return find_exact(fallback_mesh_);
}

const RenderMeshView* MeshManager::find_exact(RenderMeshHandle handle) const noexcept {
    const auto* record = find_record(handle);
    return record == nullptr ? nullptr : &record->view;
}

const RenderMeshView* MeshManager::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find_if(records_, [id](const Record& record) {
        return record.occupied && record.id == id;
    });
    return found == records_.end() ? nullptr : &found->view;
}

RenderMeshHandle MeshManager::fallback_mesh() const noexcept {
    return fallback_mesh_;
}

MeshManagerStats MeshManager::stats() noexcept {
    collect();
    refresh_stats();
    return stats_;
}

MeshManager::Record* MeshManager::find_record(RenderMeshHandle handle) noexcept {
    if (!handle.is_valid() || handle.index > records_.size()) {
        return nullptr;
    }
    auto& record = records_[handle.index - 1U];
    return record.occupied && record.generation == handle.generation ? &record : nullptr;
}

const MeshManager::Record* MeshManager::find_record(RenderMeshHandle handle) const noexcept {
    if (!handle.is_valid() || handle.index > records_.size()) {
        return nullptr;
    }
    const auto& record = records_[handle.index - 1U];
    return record.occupied && record.generation == handle.generation ? &record : nullptr;
}

void MeshManager::collect() noexcept {
    if (vertex_arena_ != nullptr) {
        vertex_arena_->collect(device_->completed_submission_serial());
    }
    if (index_arena_ != nullptr) {
        index_arena_->collect(device_->completed_submission_serial());
    }
}

void MeshManager::refresh_stats() noexcept {
    const auto uploaded_mesh_count = stats_.uploaded_mesh_count;
    const auto uploaded_bytes = stats_.uploaded_bytes;
    const auto fallback_resolution_count = stats_.fallback_resolution_count;
    stats_ = {};
    stats_.uploaded_mesh_count = uploaded_mesh_count;
    stats_.uploaded_bytes = uploaded_bytes;
    stats_.fallback_resolution_count = fallback_resolution_count;
    for (const auto& record : records_) {
        if (record.occupied) {
            ++stats_.resident_mesh_count;
            stats_.resident_mesh_bytes += record.view.vertices.size + record.view.indices.size;
        }
    }
    if (vertex_arena_ != nullptr) {
        stats_.vertex_arena = vertex_arena_->stats();
    }
    if (index_arena_ != nullptr) {
        stats_.index_arena = index_arena_->stats();
    }
}

core::Status MeshManager::shutdown() {
    auto first_failure = core::Status::ok();
    if (vertex_arena_ != nullptr && index_arena_ != nullptr) {
        for (auto& record : records_) {
            if (!record.occupied) {
                continue;
            }
            auto status = vertex_arena_->retire(record.view.vertices,
                                                device_->completed_submission_serial());
            if (!status && first_failure) {
                first_failure = status;
            }
            status = index_arena_->retire(record.view.indices,
                                          device_->completed_submission_serial());
            if (!status && first_failure) {
                first_failure = status;
            }
        }
        collect();
    }
    records_.clear();
    fallback_mesh_ = {};
    if (vertex_arena_ != nullptr) {
        auto status = vertex_arena_->shutdown();
        if (!status && first_failure) {
            first_failure = status;
        }
        vertex_arena_.reset();
    }
    if (index_arena_ != nullptr) {
        auto status = index_arena_->shutdown();
        if (!status && first_failure) {
            first_failure = status;
        }
        index_arena_.reset();
    }
    refresh_stats();
    return first_failure;
}

} // namespace heartstead::renderer
