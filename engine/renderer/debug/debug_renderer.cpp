#include "engine/renderer/debug/debug_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>
#include <span>
#include <utility>

namespace heartstead::renderer {

namespace {

[[nodiscard]] bool valid_color(const std::array<float, 4>& color) noexcept {
    return std::ranges::all_of(color, [](float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    });
}

[[nodiscard]] core::Result<world::WorldPosition>
offset_position(const world::WorldPosition& origin, math::Vec3f offset) {
    return world::WorldPosition::from_anchor(
        origin.anchor, origin.local_offset +
                           math::Vec3d{static_cast<double>(offset.x), static_cast<double>(offset.y),
                                       static_cast<double>(offset.z)});
}

[[nodiscard]] GpuDebugVertex make_vertex(math::Vec3f position,
                                         const std::array<float, 4>& color) noexcept {
    return {{position.x, position.y, position.z}, {color[0], color[1], color[2], color[3]}};
}

} // namespace

bool DebugPipelineSet::is_valid() const noexcept {
    return depth_tested.is_valid() && overlay.is_valid();
}

core::Status DebugRendererConfig::validate() const {
    constexpr auto maximum_size = std::numeric_limits<std::size_t>::max();
    if (maximum_lines == 0 || maximum_text_labels == 0 || buffered_frames < 2 ||
        buffered_frames > 8 ||
        maximum_lines > maximum_size / 2U / buffered_frames / sizeof(GpuDebugVertex) ||
        maximum_lines > maximum_size / 2U / buffered_frames / sizeof(std::uint32_t)) {
        return core::Status::failure(
            "debug_renderer.invalid_config",
            "debug renderer capacities must be nonzero and use two to eight buffered frames");
    }
    return core::Status::ok();
}

core::Status validate_debug_line(const DebugLineDesc& line) {
    if (!line.start.is_valid() || !line.end.is_valid() || !valid_color(line.color) ||
        !std::isfinite(line.lifetime_seconds) || line.lifetime_seconds < 0.0F) {
        return core::Status::failure("debug_renderer.invalid_line",
                                     "debug line contains invalid position, color, or lifetime");
    }
    return core::Status::ok();
}

core::Status validate_debug_text_label(const DebugTextLabelDesc& label) {
    if (!label.position.is_valid() || label.text.empty() || !valid_color(label.color) ||
        !std::isfinite(label.lifetime_seconds) || label.lifetime_seconds < 0.0F) {
        return core::Status::failure("debug_renderer.invalid_text",
                                     "debug text contains invalid position, text, color, or lifetime");
    }
    return core::Status::ok();
}

DebugRenderer::DebugRenderer(rhi::IRenderDevice& device, DebugPipelineSet pipelines)
    : device_(&device), pipelines_(pipelines) {}

DebugRenderer::~DebugRenderer() {
    (void)shutdown();
}

core::Status DebugRenderer::initialize(DebugRendererConfig config) {
    if (vertex_buffer_.is_valid() || index_buffer_.is_valid()) {
        return core::Status::failure("debug_renderer.already_initialized",
                                     "debug renderer cannot be initialized twice");
    }
    auto status = config.validate();
    if (!status) {
        return status;
    }
    if (!pipelines_.is_valid()) {
        return core::Status::failure("debug_renderer.invalid_pipeline",
                                     "debug renderer requires depth and overlay pipelines");
    }
    config_ = config;
    const auto vertex_count = static_cast<std::size_t>(config_.maximum_lines) * 2U *
                              config_.buffered_frames;
    auto vertices = device_->create_buffer({rhi::RenderBufferUsage::vertex,
                                            vertex_count * sizeof(GpuDebugVertex),
                                            "debug_line_vertices",
                                            rhi::RenderBufferMemory::device_local});
    if (!vertices) {
        return core::Status::failure(vertices.error().code, vertices.error().message);
    }
    vertex_buffer_ = vertices.value().handle;
    auto indices = device_->create_buffer({rhi::RenderBufferUsage::index,
                                           vertex_count * sizeof(std::uint32_t),
                                           "debug_line_indices",
                                           rhi::RenderBufferMemory::device_local});
    if (!indices) {
        const auto error = indices.error();
        (void)device_->release_resource(vertex_buffer_);
        vertex_buffer_ = {};
        return core::Status::failure(error.code, error.message);
    }
    index_buffer_ = indices.value().handle;
    pending_lines_.reserve(config_.maximum_lines);
    active_lines_.reserve(config_.maximum_lines);
    pending_labels_.reserve(config_.maximum_text_labels);
    active_labels_.reserve(config_.maximum_text_labels);
    vertex_scratch_.reserve(static_cast<std::size_t>(config_.maximum_lines) * 2U);
    index_scratch_.reserve(static_cast<std::size_t>(config_.maximum_lines) * 2U);
    return core::Status::ok();
}

core::Status DebugRenderer::submit_line(DebugLineDesc line) {
    auto status = validate_debug_line(line);
    if (!status) {
        return status;
    }
    std::scoped_lock lock(mutex_);
    if (pending_lines_.size() + active_lines_.size() >= config_.maximum_lines) {
        ++overflowed_lines_;
        return core::Status::failure("debug_renderer.line_capacity_exhausted",
                                     "debug line capacity is exhausted");
    }
    pending_lines_.push_back(std::move(line));
    return core::Status::ok();
}

core::Status DebugRenderer::submit_ray(const world::WorldPosition& origin, math::Vec3f direction,
                                       float length, std::array<float, 4> color,
                                       float lifetime_seconds, DebugDepthMode depth) {
    const auto direction_length = static_cast<float>(math::length(direction));
    if (!std::isfinite(length) || length <= 0.0F || !std::isfinite(direction_length) ||
        direction_length <= 0.0F) {
        return core::Status::failure("debug_renderer.invalid_ray",
                                     "debug ray direction and length must be finite and nonzero");
    }
    auto end = offset_position(origin, direction / direction_length * length);
    if (!end) {
        return core::Status::failure(end.error().code, end.error().message);
    }
    return submit_line({origin, end.value(), color, lifetime_seconds, depth});
}

core::Status DebugRenderer::submit_aabb(const world::WorldPosition& origin,
                                        const math::Bounds3f& bounds,
                                        std::array<float, 4> color, float lifetime_seconds,
                                        DebugDepthMode depth) {
    return submit_oriented_box(origin, bounds, {}, color, lifetime_seconds, depth);
}

core::Status DebugRenderer::submit_oriented_box(const world::WorldPosition& origin,
                                                const math::Bounds3f& bounds,
                                                const math::Transform3f& transform,
                                                std::array<float, 4> color, float lifetime_seconds,
                                                DebugDepthMode depth) {
    if (!origin.is_valid() || !bounds.is_valid() || !transform.is_finite() ||
        !transform.has_non_zero_scale()) {
        return core::Status::failure("debug_renderer.invalid_box",
                                     "debug box origin, bounds, or transform is invalid");
    }
    const auto matrix = math::transform_matrix(transform);
    std::array<world::WorldPosition, 8> corners;
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const math::Vec4f local{(index & 1U) != 0 ? bounds.max.x : bounds.min.x,
                                (index & 2U) != 0 ? bounds.max.y : bounds.min.y,
                                (index & 4U) != 0 ? bounds.max.z : bounds.min.z, 1.0F};
        const auto transformed = matrix * local;
        auto corner = offset_position(origin, {transformed.x, transformed.y, transformed.z});
        if (!corner) {
            return core::Status::failure(corner.error().code, corner.error().message);
        }
        corners[index] = corner.value();
    }
    constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
        {0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
        {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    for (const auto edge : edges) {
        auto status = submit_line(
            {corners[edge[0]], corners[edge[1]], color, lifetime_seconds, depth});
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

core::Status DebugRenderer::submit_sphere(const world::WorldPosition& origin, float radius,
                                          std::array<float, 4> color, std::uint32_t segments,
                                          float lifetime_seconds, DebugDepthMode depth) {
    if (!origin.is_valid() || !std::isfinite(radius) || radius <= 0.0F || segments < 8 ||
        segments > 256) {
        return core::Status::failure("debug_renderer.invalid_sphere",
                                     "debug sphere radius or segment count is invalid");
    }
    constexpr auto tau = std::numbers::pi_v<float> * 2.0F;
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const auto first_angle = tau * static_cast<float>(segment) /
                                     static_cast<float>(segments);
            const auto second_angle = tau * static_cast<float>(segment + 1U) /
                                      static_cast<float>(segments);
            const auto point = [axis, radius](float angle) {
                const auto first = std::cos(angle) * radius;
                const auto second = std::sin(angle) * radius;
                switch (axis) {
                case 0:
                    return math::Vec3f{0.0F, first, second};
                case 1:
                    return math::Vec3f{first, 0.0F, second};
                default:
                    return math::Vec3f{first, second, 0.0F};
                }
            };
            auto first = offset_position(origin, point(first_angle));
            auto second = offset_position(origin, point(second_angle));
            if (!first || !second) {
                const auto& error = !first ? first.error() : second.error();
                return core::Status::failure(error.code, error.message);
            }
            auto status =
                submit_line({first.value(), second.value(), color, lifetime_seconds, depth});
            if (!status) {
                return status;
            }
        }
    }
    return core::Status::ok();
}

core::Status DebugRenderer::submit_axes(const world::WorldPosition& origin, float scale,
                                        float lifetime_seconds, DebugDepthMode depth) {
    auto status = submit_ray(origin, {1.0F, 0.0F, 0.0F}, scale, {1.0F, 0.1F, 0.1F, 1.0F},
                             lifetime_seconds, depth);
    if (!status) {
        return status;
    }
    status = submit_ray(origin, {0.0F, 1.0F, 0.0F}, scale, {0.1F, 1.0F, 0.1F, 1.0F},
                        lifetime_seconds, depth);
    if (!status) {
        return status;
    }
    return submit_ray(origin, {0.0F, 0.0F, 1.0F}, scale, {0.1F, 0.4F, 1.0F, 1.0F},
                      lifetime_seconds, depth);
}

core::Status DebugRenderer::submit_text(DebugTextLabelDesc label) {
    auto status = validate_debug_text_label(label);
    if (!status) {
        return status;
    }
    std::scoped_lock lock(mutex_);
    if (pending_labels_.size() + active_labels_.size() >= config_.maximum_text_labels) {
        ++overflowed_labels_;
        return core::Status::failure("debug_renderer.text_capacity_exhausted",
                                     "debug text capacity is exhausted");
    }
    pending_labels_.push_back(std::move(label));
    return core::Status::ok();
}

core::Result<DebugFrameCommands>
DebugRenderer::build_frame(const RenderCamera& camera, float delta_seconds,
                           DebugFrameCommands scratch) {
    if (!vertex_buffer_.is_valid() || !index_buffer_.is_valid()) {
        return core::Result<DebugFrameCommands>::failure(
            "debug_renderer.not_initialized", "debug renderer must be initialized first");
    }
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F) {
        return core::Result<DebugFrameCommands>::failure(
            "debug_renderer.invalid_delta", "debug frame delta must be finite and nonnegative");
    }
    std::unique_lock lock(mutex_);
    active_lines_.insert(active_lines_.end(),
                         std::make_move_iterator(pending_lines_.begin()),
                         std::make_move_iterator(pending_lines_.end()));
    pending_lines_.clear();
    active_labels_.insert(active_labels_.end(),
                          std::make_move_iterator(pending_labels_.begin()),
                          std::make_move_iterator(pending_labels_.end()));
    pending_labels_.clear();
    scratch.draws.clear();
    scratch.text_labels.clear();
    vertex_scratch_.clear();
    index_scratch_.clear();
    const auto frame_slot = frame_number_ % config_.buffered_frames;
    const auto segment_vertex_capacity = static_cast<std::uint64_t>(config_.maximum_lines) * 2U;
    const auto segment_vertex_offset = frame_slot * segment_vertex_capacity;
    const auto emit_mode = [&](DebugDepthMode mode) -> core::Result<std::uint32_t> {
        const auto first_vertex = vertex_scratch_.size();
        std::uint32_t emitted = 0;
        for (const auto& line : active_lines_) {
            if (line.depth_mode != mode) {
                continue;
            }
            auto start = world::to_camera_relative(line.start, camera.floating_origin);
            auto end = world::to_camera_relative(line.end, camera.floating_origin);
            if (!start || !end) {
                continue;
            }
            vertex_scratch_.push_back(make_vertex(start.value(), line.color));
            vertex_scratch_.push_back(make_vertex(end.value(), line.color));
            index_scratch_.push_back(emitted * 2U);
            index_scratch_.push_back(emitted * 2U + 1U);
            ++emitted;
        }
        if (emitted == 0) {
            return core::Result<std::uint32_t>::success(0);
        }
        const auto first_index_in_segment = index_scratch_.size() - emitted * 2U;
        const auto absolute_first_index = segment_vertex_offset + first_index_in_segment;
        const auto absolute_vertex_offset = segment_vertex_offset + first_vertex;
        if (absolute_first_index > std::numeric_limits<std::uint32_t>::max() ||
            absolute_vertex_offset >
                static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return core::Result<std::uint32_t>::failure(
                "debug_renderer.draw_offset_overflow", "debug buffered draw offset exceeds RHI range");
        }
        rhi::RenderDrawCommand draw;
        draw.pipeline = mode == DebugDepthMode::depth_tested ? pipelines_.depth_tested
                                                             : pipelines_.overlay;
        draw.vertex_buffer = vertex_buffer_;
        draw.index_buffer = index_buffer_;
        draw.index_count = emitted * 2U;
        draw.first_index = static_cast<std::uint32_t>(absolute_first_index);
        draw.vertex_offset = static_cast<std::int32_t>(absolute_vertex_offset);
        draw.index_type = rhi::RenderIndexType::uint32;
        scratch.draws.push_back(draw);
        return core::Result<std::uint32_t>::success(emitted);
    };
    auto depth_lines = emit_mode(DebugDepthMode::depth_tested);
    auto overlay_lines = emit_mode(DebugDepthMode::overlay);
    if (!depth_lines || !overlay_lines) {
        const auto& error = !depth_lines ? depth_lines.error() : overlay_lines.error();
        return core::Result<DebugFrameCommands>::failure(error.code, error.message);
    }
    for (const auto& label : active_labels_) {
        auto position = world::to_camera_relative(label.position, camera.floating_origin);
        if (position) {
            scratch.text_labels.push_back(
                {position.value(), label.text, label.color, label.depth_mode});
        }
    }
    stats_ = {};
    stats_.active_lines = static_cast<std::uint32_t>(active_lines_.size());
    stats_.active_text_labels = static_cast<std::uint32_t>(active_labels_.size());
    stats_.submitted_lines = depth_lines.value() + overlay_lines.value();
    stats_.draw_calls = static_cast<std::uint32_t>(scratch.draws.size());
    stats_.overflowed_lines = overflowed_lines_;
    stats_.overflowed_text_labels = overflowed_labels_;
    const auto expire = [delta_seconds](auto& values) {
        values.erase(std::remove_if(values.begin(), values.end(), [delta_seconds](auto& value) {
                         if (value.lifetime_seconds == 0.0F) {
                             return true;
                         }
                         value.lifetime_seconds =
                             std::max(0.0F, value.lifetime_seconds - delta_seconds);
                         return value.lifetime_seconds == 0.0F;
                     }),
                     values.end());
    };
    expire(active_lines_);
    expire(active_labels_);
    lock.unlock();
    if (!vertex_scratch_.empty()) {
        const auto vertex_bytes = std::as_bytes(std::span<const GpuDebugVertex>{vertex_scratch_});
        const auto index_bytes = std::as_bytes(std::span<const std::uint32_t>{index_scratch_});
        const std::array writes{
            rhi::RenderBufferWrite{vertex_buffer_,
                                   static_cast<std::size_t>(segment_vertex_offset) *
                                       sizeof(GpuDebugVertex),
                                   vertex_bytes},
            rhi::RenderBufferWrite{index_buffer_,
                                   static_cast<std::size_t>(segment_vertex_offset) *
                                       sizeof(std::uint32_t),
                                   index_bytes},
        };
        auto upload = device_->upload_buffer_batch(writes);
        if (!upload) {
            return core::Result<DebugFrameCommands>::failure(upload.error().code,
                                                             upload.error().message);
        }
        stats_.uploaded_bytes = vertex_bytes.size() + index_bytes.size();
    }
    ++frame_number_;
    scratch.stats = stats_;
    return core::Result<DebugFrameCommands>::success(std::move(scratch));
}

void DebugRenderer::clear() noexcept {
    std::scoped_lock lock(mutex_);
    pending_lines_.clear();
    active_lines_.clear();
    pending_labels_.clear();
    active_labels_.clear();
    vertex_scratch_.clear();
    index_scratch_.clear();
    stats_ = {};
}

core::Status DebugRenderer::set_pipelines(DebugPipelineSet pipelines) noexcept {
    if (!pipelines.is_valid()) {
        return core::Status::failure("debug_renderer.invalid_pipeline",
                                     "debug pipelines must both be valid");
    }
    pipelines_ = pipelines;
    return core::Status::ok();
}

core::Status DebugRenderer::shutdown() {
    clear();
    frame_number_ = 0;
    auto first_failure = core::Status::ok();
    if (vertex_buffer_.is_valid()) {
        first_failure = device_->release_resource(vertex_buffer_);
        vertex_buffer_ = {};
    }
    if (index_buffer_.is_valid()) {
        auto status = device_->release_resource(index_buffer_);
        if (!status && first_failure) {
            first_failure = status;
        }
        index_buffer_ = {};
    }
    return first_failure;
}

const DebugRendererStats& DebugRenderer::stats() const noexcept {
    return stats_;
}

} // namespace heartstead::renderer
