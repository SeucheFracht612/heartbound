#include "engine/renderer/chunks/chunk_render_system.hpp"

#include "engine/world/blocks/block_model.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

namespace heartstead::renderer {

namespace {

[[nodiscard]] bool region_contains(const dirty::DirtyRegion& region,
                                   world::ChunkCoord coordinate) noexcept {
    return coordinate.x >= region.bounds.min.x && coordinate.x <= region.bounds.max.x &&
           coordinate.y >= region.bounds.min.y && coordinate.y <= region.bounds.max.y &&
           coordinate.z >= region.bounds.min.z && coordinate.z <= region.bounds.max.z;
}

[[nodiscard]] math::Bounds3f translated_bounds(math::Bounds3f bounds, math::Vec3f origin) noexcept {
    if (!bounds.is_valid()) {
        return bounds;
    }
    return {bounds.min + origin, bounds.max + origin};
}

[[nodiscard]] math::Bounds3f default_chunk_bounds() noexcept {
    constexpr auto edge = static_cast<float>(world::VoxelChunk::edge_length);
    return {{0.0F, 0.0F, 0.0F}, {edge, edge, edge}};
}

} // namespace

core::Status ChunkRenderConfig::validate() const {
    if (max_chunks_meshed_per_frame == 0) {
        return core::Status::failure("renderer.invalid_chunk_mesh_budget",
                                     "chunk mesh budget must allow at least one chunk");
    }
    if (max_bytes_uploaded_per_frame == 0) {
        return core::Status::failure("renderer.invalid_chunk_upload_budget",
                                     "chunk upload budget must allow at least one byte");
    }
    return core::Status::ok();
}

std::size_t ChunkRenderSystem::PendingUpload::byte_size() const noexcept {
    return vertices.size() * sizeof(terrain::GpuChunkVertex) +
           indices.size() * sizeof(std::uint32_t);
}

ChunkRenderSystem::ChunkRenderSystem(ChunkGpuCache& cache,
                                     rhi::RenderResourceHandle terrain_pipeline,
                                     const world::VoxelPalette* palette, ChunkRenderConfig config)
    : cache_(&cache), terrain_pipeline_(terrain_pipeline), palette_(palette), config_(config) {}

core::Status
ChunkRenderSystem::process_chunk_loads(std::span<const world::ChunkStreamLoadReport> loads) {
    for (const auto& load : loads) {
        if (!load.identity.is_valid()) {
            return core::Status::failure("renderer.invalid_chunk_load_identity",
                                         "chunk load report has no load generation");
        }
        if (!cache_->contains(load.identity)) {
            const auto* old_entry = cache_->find_by_coordinate(load.identity.coordinate);
            if (old_entry != nullptr) {
                const auto old_identity = old_entry->identity;
                remove_pending(old_identity);
                auto erase_status = cache_->erase(old_identity);
                if (!erase_status) {
                    return erase_status;
                }
            }
            auto insert_status = cache_->insert(load.identity);
            if (!insert_status) {
                return insert_status;
            }
        }
        enqueue_mesh(load.identity, false);
    }
    refresh_queue_stats();
    return core::Status::ok();
}

core::Status
ChunkRenderSystem::process_chunk_evictions(std::span<const world::ChunkIdentity> evictions) {
    core::Status first_failure = core::Status::ok();
    for (const auto identity : evictions) {
        remove_pending(identity);
        if (!cache_->contains(identity)) {
            // A stale eviction must never remove a newer generation at the same coordinate.
            continue;
        }
        auto status = cache_->erase(identity);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    refresh_queue_stats();
    return first_failure;
}

core::Status
ChunkRenderSystem::process_chunk_evictions(const world::ChunkStreamEvictionReport& eviction) {
    return process_chunk_evictions(eviction.evicted_identities);
}

core::Status ChunkRenderSystem::synchronize(world::WorldState& world, const RenderCamera& camera) {
    auto status = config_.validate();
    if (!status) {
        return status;
    }
    if (!terrain_pipeline_.is_valid()) {
        return core::Status::failure("renderer.invalid_terrain_pipeline",
                                     "chunk render system requires a terrain pipeline");
    }

    stats_.meshed_chunk_count = 0;
    stats_.uploaded_chunk_count = 0;
    stats_.uploaded_bytes = 0;
    stats_.failed_mesh_count = 0;
    stats_.failed_upload_count = 0;
    timings_.reset();

    status = reconcile_loaded_chunks(world);
    if (!status) {
        return status;
    }
    consume_dirty_regions(world);

    core::Status first_failure = process_mesh_queue(world, camera);
    auto upload_status = process_upload_queue(world, camera);
    if (!upload_status && first_failure) {
        first_failure = upload_status;
    }
    stats_.cache = cache_->stats();
    refresh_queue_stats();
    refresh_timing_stats();
    return first_failure;
}

ChunkDrawList ChunkRenderSystem::build_draw_list(const RenderCamera& camera) {
    ChunkDrawList result;
    struct VisibleEntry {
        const ChunkGpuEntry* entry = nullptr;
        math::Vec3f origin{};
    };
    std::vector<VisibleEntry> visible_entries;
    const auto frustum = RenderFrustum::from_view_projection(camera.view_projection);
    {
        profiling::ScopedCpuTimingZone culling_zone(timings_,
                                                    profiling::CpuTimingZone::visibility_culling);
        for (const auto* entry : cache_->entries()) {
            if (entry->state != ChunkGpuState::resident) {
                continue;
            }
            const auto origin = camera_relative_chunk_origin(entry->identity.coordinate, camera);
            if (!origin ||
                !frustum.intersects(translated_bounds(entry->local_bounds, origin.value()))) {
                ++result.culled_chunk_count;
                continue;
            }
            ++result.visible_chunk_count;
            visible_entries.push_back({entry, origin.value()});
        }
    }

    {
        profiling::ScopedCpuTimingZone draw_zone(timings_,
                                                 profiling::CpuTimingZone::draw_list_construction);
        for (const auto& visible : visible_entries) {
            if (!visible.entry->has_drawable_mesh()) {
                continue;
            }
            rhi::RenderDrawCommand draw;
            draw.pipeline = terrain_pipeline_;
            draw.vertex_buffer = visible.entry->vertex_buffer;
            draw.index_buffer = visible.entry->index_buffer;
            draw.index_count = visible.entry->index_count;
            draw.instance_count = 1;
            draw.camera_relative_origin = visible.origin;
            result.draws.push_back(draw);
            result.vertex_count += visible.entry->vertex_count;
            result.index_count += visible.entry->index_count;
        }
        std::ranges::stable_sort(result.draws, [](const rhi::RenderDrawCommand& left,
                                                  const rhi::RenderDrawCommand& right) {
            return left.pipeline.value < right.pipeline.value;
        });
    }
    stats_.visible_chunk_count = result.visible_chunk_count;
    stats_.culled_chunk_count = result.culled_chunk_count;
    stats_.draw_count = result.draws.size();
    stats_.visible_vertex_count = result.vertex_count;
    stats_.visible_index_count = result.index_count;
    refresh_timing_stats();
    return result;
}

const ChunkRenderStats& ChunkRenderSystem::stats() const noexcept {
    return stats_;
}

void ChunkRenderSystem::enqueue_mesh(world::ChunkIdentity identity, bool forced) {
    const auto found =
        std::ranges::find_if(pending_meshes_, [identity](const PendingMesh& pending) {
            return pending.identity == identity;
        });
    if (found != pending_meshes_.end()) {
        found->forced = found->forced || forced;
        return;
    }
    pending_meshes_.push_back({identity, forced, next_sequence_++});
}

void ChunkRenderSystem::remove_pending(world::ChunkIdentity identity) {
    std::erase_if(pending_meshes_,
                  [identity](const PendingMesh& pending) { return pending.identity == identity; });
    std::erase_if(pending_uploads_, [identity](const PendingUpload& pending) {
        return pending.identity == identity;
    });
}

core::Status ChunkRenderSystem::reconcile_loaded_chunks(world::WorldState& world) {
    const auto loaded = world.chunks().identities();
    for (const auto identity : loaded) {
        const auto* existing = cache_->find_by_coordinate(identity.coordinate);
        if (existing != nullptr && existing->identity != identity) {
            const auto old_identity = existing->identity;
            remove_pending(old_identity);
            auto erase_status = cache_->erase(old_identity);
            if (!erase_status) {
                return erase_status;
            }
        }
        if (!cache_->contains(identity)) {
            auto insert_status = cache_->insert(identity);
            if (!insert_status) {
                return insert_status;
            }
            enqueue_mesh(identity, false);
        }

        const auto* chunk = world.chunks().find(identity.coordinate);
        const auto* entry = cache_->find(identity);
        if (chunk == nullptr || entry == nullptr || chunk->identity() != identity) {
            continue;
        }
        const auto queued_upload =
            std::ranges::find_if(pending_uploads_, [identity](const PendingUpload& pending) {
                return pending.identity == identity;
            });
        const bool newest_revision_awaits_upload =
            queued_upload != pending_uploads_.end() &&
            queued_upload->content_revision == chunk->content_revision();
        const bool stale_revision = entry->state != ChunkGpuState::resident ||
                                    entry->resident_content_revision != chunk->content_revision();
        if (!newest_revision_awaits_upload &&
            (stale_revision || chunk->dirty().contains(world::ChunkDirtyFlag::mesh))) {
            enqueue_mesh(identity, chunk->dirty().contains(world::ChunkDirtyFlag::mesh));
        }
    }

    std::vector<world::ChunkIdentity> unloaded;
    for (const auto* entry : cache_->entries()) {
        if (!std::ranges::binary_search(loaded, entry->identity)) {
            unloaded.push_back(entry->identity);
        }
    }
    return process_chunk_evictions(unloaded);
}

void ChunkRenderSystem::consume_dirty_regions(world::WorldState& world) {
    const auto regions = world.dirty_regions().consume_kind(dirty::DirtyRegionKind::chunk_mesh);
    if (regions.empty()) {
        return;
    }
    for (const auto identity : world.chunks().identities()) {
        if (std::ranges::any_of(regions, [identity](const dirty::DirtyRegion& region) {
                return region_contains(region, identity.coordinate);
            })) {
            enqueue_mesh(identity, true);
        }
    }
}

core::Status ChunkRenderSystem::process_mesh_queue(world::WorldState& world,
                                                   const RenderCamera& camera) {
    prioritize_mesh_queue(world, camera);
    core::Status first_failure = core::Status::ok();
    const auto available_at_start = pending_meshes_.size();
    std::size_t attempted = 0;
    while (!pending_meshes_.empty() && attempted < available_at_start &&
           stats_.meshed_chunk_count < config_.max_chunks_meshed_per_frame) {
        auto pending = pending_meshes_.front();
        pending_meshes_.erase(pending_meshes_.begin());
        ++attempted;

        auto* chunk = world.chunks().find(pending.identity.coordinate);
        if (chunk == nullptr || chunk->identity() != pending.identity ||
            !cache_->contains(pending.identity)) {
            continue;
        }
        std::uint64_t requested_revision = 0;
        {
            profiling::ScopedCpuTimingZone snapshot_zone(timings_,
                                                         profiling::CpuTimingZone::chunk_snapshot);
            // Milestone 2 still meshes synchronously from live storage. This zone measures the
            // identity/revision capture and becomes the immutable snapshot copy zone before jobs.
            requested_revision = chunk->content_revision();
        }
        world::ChunkMeshingContext context{
            *chunk,
            palette_,
            &world.chunks(),
            world::BlockModelDefinition::max_dependency_radius,
        };
        auto mesh = [&]() {
            profiling::ScopedCpuTimingZone meshing_zone(timings_,
                                                        profiling::CpuTimingZone::meshing);
            return world::ChunkMesher::build_surface_mesh(context);
        }();
        ++stats_.meshed_chunk_count;
        if (!mesh) {
            ++stats_.failed_mesh_count;
            cache_->mark_failed(pending.identity);
            pending.sequence = next_sequence_++;
            pending_meshes_.push_back(pending);
            if (first_failure) {
                first_failure = core::Status::failure(mesh.error().code, mesh.error().message);
            }
            continue;
        }
        if (chunk->content_revision() != requested_revision ||
            chunk->identity() != pending.identity) {
            enqueue_mesh(pending.identity, true);
            continue;
        }

        std::erase_if(pending_uploads_, [&pending](const PendingUpload& upload) {
            return upload.identity == pending.identity;
        });
        {
            profiling::ScopedCpuTimingZone preparation_zone(
                timings_, profiling::CpuTimingZone::upload_preparation);
            auto built_mesh = std::move(mesh).value();
            PendingUpload upload;
            upload.identity = pending.identity;
            upload.content_revision = requested_revision;
            upload.local_bounds = built_mesh.local_bounds;
            upload.vertices = terrain::make_gpu_chunk_vertices(built_mesh.vertices);
            upload.indices = std::move(built_mesh.indices);
            upload.forced = pending.forced;
            upload.sequence = pending.sequence;
            pending_uploads_.push_back(std::move(upload));
        }
        cache_->mark_cpu_mesh_ready(pending.identity);
        cache_->mark_upload_pending(pending.identity);
    }
    return first_failure;
}

core::Status ChunkRenderSystem::process_upload_queue(world::WorldState& world,
                                                     const RenderCamera& camera) {
    prioritize_upload_queue(camera);
    core::Status first_failure = core::Status::ok();
    const auto available_at_start = pending_uploads_.size();
    std::size_t attempted = 0;
    while (!pending_uploads_.empty() && attempted < available_at_start) {
        const auto bytes = pending_uploads_.front().byte_size();
        const bool budget_available =
            stats_.uploaded_bytes <= config_.max_bytes_uploaded_per_frame &&
            bytes <= config_.max_bytes_uploaded_per_frame - stats_.uploaded_bytes;
        if (!budget_available && stats_.uploaded_chunk_count > 0) {
            break;
        }

        auto pending = std::move(pending_uploads_.front());
        pending_uploads_.erase(pending_uploads_.begin());
        ++attempted;
        auto* chunk = world.chunks().find(pending.identity.coordinate);
        if (chunk == nullptr || chunk->identity() != pending.identity ||
            !cache_->contains(pending.identity)) {
            continue;
        }
        if (chunk->content_revision() != pending.content_revision) {
            enqueue_mesh(pending.identity, true);
            continue;
        }

        auto upload = [&]() {
            profiling::ScopedCpuTimingZone upload_zone(timings_, profiling::CpuTimingZone::upload);
            return cache_->replace_mesh(pending.identity, pending.content_revision,
                                        pending.local_bounds, pending.vertices, pending.indices);
        }();
        if (!upload) {
            ++stats_.failed_upload_count;
            pending.sequence = next_sequence_++;
            pending_uploads_.push_back(std::move(pending));
            if (first_failure) {
                first_failure = core::Status::failure(upload.error().code, upload.error().message);
            }
            continue;
        }

        ++stats_.uploaded_chunk_count;
        stats_.uploaded_bytes += upload.value().uploaded_bytes;
        if (chunk->identity() == pending.identity &&
            chunk->content_revision() == pending.content_revision) {
            chunk->clear_dirty(world::ChunkDirtyFlag::mesh);
        } else {
            enqueue_mesh(pending.identity, true);
        }
    }
    return first_failure;
}

void ChunkRenderSystem::prioritize_mesh_queue(const world::WorldState& world,
                                              const RenderCamera& camera) {
    std::ranges::stable_sort(pending_meshes_, [this, &world, &camera](const PendingMesh& left,
                                                                      const PendingMesh& right) {
        const auto* left_entry = cache_->find(left.identity);
        const auto* right_entry = cache_->find(right.identity);
        const auto left_bounds = left_entry != nullptr && left_entry->local_bounds.is_valid()
                                     ? left_entry->local_bounds
                                     : default_chunk_bounds();
        const auto right_bounds = right_entry != nullptr && right_entry->local_bounds.is_valid()
                                      ? right_entry->local_bounds
                                      : default_chunk_bounds();
        const bool left_visible = is_visible(left.identity.coordinate, left_bounds, camera);
        const bool right_visible = is_visible(right.identity.coordinate, right_bounds, camera);
        if (left_visible != right_visible) {
            return left_visible;
        }
        const auto left_distance = distance_squared(left.identity.coordinate, left_bounds, camera);
        const auto right_distance =
            distance_squared(right.identity.coordinate, right_bounds, camera);
        if (left_distance != right_distance) {
            return left_distance < right_distance;
        }
        return left.sequence < right.sequence;
    });
    (void)world;
}

void ChunkRenderSystem::prioritize_upload_queue(const RenderCamera& camera) {
    std::ranges::stable_sort(pending_uploads_, [this, &camera](const PendingUpload& left,
                                                               const PendingUpload& right) {
        const bool left_visible = is_visible(left.identity.coordinate, left.local_bounds, camera);
        const bool right_visible =
            is_visible(right.identity.coordinate, right.local_bounds, camera);
        if (left_visible != right_visible) {
            return left_visible;
        }
        const auto left_distance =
            distance_squared(left.identity.coordinate, left.local_bounds, camera);
        const auto right_distance =
            distance_squared(right.identity.coordinate, right.local_bounds, camera);
        if (left_distance != right_distance) {
            return left_distance < right_distance;
        }
        return left.sequence < right.sequence;
    });
}

bool ChunkRenderSystem::is_visible(world::ChunkCoord coord, math::Bounds3f local_bounds,
                                   const RenderCamera& camera) const {
    const auto origin = camera_relative_chunk_origin(coord, camera);
    if (!origin) {
        return false;
    }
    const auto frustum = RenderFrustum::from_view_projection(camera.view_projection);
    return frustum.intersects(translated_bounds(local_bounds, origin.value()));
}

float ChunkRenderSystem::distance_squared(world::ChunkCoord coord, math::Bounds3f local_bounds,
                                          const RenderCamera& camera) const {
    const auto origin = camera_relative_chunk_origin(coord, camera);
    if (!origin) {
        return std::numeric_limits<float>::max();
    }
    const auto center = local_bounds.is_valid()
                            ? (local_bounds.min + local_bounds.max) * 0.5F + origin.value()
                            : origin.value();
    return math::length_squared(center - camera.local_position);
}

core::Result<math::Vec3f>
ChunkRenderSystem::camera_relative_chunk_origin(world::ChunkCoord coord,
                                                const RenderCamera& camera) {
    auto block = world::chunk_local_to_block(coord, {0, 0, 0});
    if (!block) {
        return core::Result<math::Vec3f>::failure(block.error().code, block.error().message);
    }
    world::WorldPosition position;
    position.anchor = block.value();
    return world::to_camera_relative(position, camera.floating_origin);
}

void ChunkRenderSystem::refresh_queue_stats() noexcept {
    stats_.cache = cache_->stats();
    stats_.pending_mesh_count = pending_meshes_.size();
    stats_.pending_upload_count = pending_uploads_.size();
    stats_.pending_upload_bytes = 0;
    for (const auto& upload : pending_uploads_) {
        stats_.pending_upload_bytes += upload.byte_size();
    }
}

void ChunkRenderSystem::refresh_timing_stats() noexcept {
    stats_.culling_ms = timings_.milliseconds(profiling::CpuTimingZone::visibility_culling);
    stats_.draw_list_ms = timings_.milliseconds(profiling::CpuTimingZone::draw_list_construction);
    stats_.chunk_snapshot_ms = timings_.milliseconds(profiling::CpuTimingZone::chunk_snapshot);
    stats_.meshing_ms = timings_.milliseconds(profiling::CpuTimingZone::meshing);
    stats_.upload_preparation_ms =
        timings_.milliseconds(profiling::CpuTimingZone::upload_preparation);
    stats_.upload_ms = timings_.milliseconds(profiling::CpuTimingZone::upload);
}

} // namespace heartstead::renderer
