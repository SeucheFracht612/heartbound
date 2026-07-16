#include "engine/renderer/materials/block_render_table.hpp"

#include <algorithm>
#include <bit>
#include <exception>
#include <limits>

namespace heartstead::renderer {

namespace {

[[nodiscard]] std::size_t next_capacity(std::size_t required) noexcept {
    constexpr std::size_t minimum = 4096;
    return std::max(minimum, std::bit_ceil(required));
}

} // namespace

BlockRenderTable::BlockRenderTable() : materials_(1) {}

core::Status BlockRenderTable::set(std::uint16_t voxel_type, GpuVoxelMaterial material) {
    if (voxel_type == 0) {
        return core::Status::failure("block_render_table.air_material_rejected",
                                     "voxel type zero is reserved for air");
    }
    const bool grew = voxel_type >= materials_.size();
    if (grew) {
        materials_.resize(static_cast<std::size_t>(voxel_type) + 1U);
    }
    if (!grew && materials_[voxel_type] == material) {
        return core::Status::ok();
    }
    materials_[voxel_type] = material;
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        std::terminate();
    }
    ++revision_;
    return core::Status::ok();
}

const GpuVoxelMaterial& BlockRenderTable::get(std::uint16_t voxel_type) const noexcept {
    return voxel_type < materials_.size() ? materials_[voxel_type] : materials_.front();
}

std::span<const GpuVoxelMaterial> BlockRenderTable::materials() const noexcept {
    return materials_;
}

std::uint64_t BlockRenderTable::revision() const noexcept {
    return revision_;
}

GpuMaterialTable::GpuMaterialTable(rhi::IRenderDevice& device) : device_(device) {}

GpuMaterialTable::~GpuMaterialTable() {
    (void)shutdown();
}

core::Status GpuMaterialTable::synchronize(const BlockRenderTable& table) {
    if (stats_.resident_revision == table.revision()) {
        return core::Status::ok();
    }
    const auto required_bytes = table.materials().size_bytes();
    auto destination = buffer_;
    auto destination_capacity = capacity_bytes_;
    bool replacement = false;
    if (!destination.is_valid() || required_bytes > destination_capacity) {
        destination_capacity = next_capacity(required_bytes);
        auto created = device_.create_buffer({rhi::RenderBufferUsage::storage, destination_capacity,
                                              "gpu_voxel_material_table",
                                              rhi::RenderBufferMemory::device_local});
        if (!created) {
            return core::Status::failure(created.error().code, created.error().message);
        }
        destination = created.value().handle;
        replacement = true;
    }
    const auto bytes = std::as_bytes(table.materials());
    const rhi::RenderBufferWrite write{destination, 0, bytes};
    auto uploaded = device_.upload_buffer_batch(std::span<const rhi::RenderBufferWrite>{&write, 1});
    if (!uploaded) {
        if (replacement) {
            (void)device_.release_resource(destination);
        }
        return core::Status::failure(uploaded.error().code, uploaded.error().message);
    }
    if (replacement) {
        const auto old = buffer_;
        buffer_ = destination;
        capacity_bytes_ = destination_capacity;
        if (old.is_valid()) {
            auto status = device_.release_resource(old);
            if (!status) {
                return status;
            }
        }
    }
    stats_.material_count = table.materials().size();
    stats_.resident_bytes = required_bytes;
    stats_.capacity_bytes = capacity_bytes_;
    stats_.resident_revision = table.revision();
    ++stats_.upload_count;
    stats_.uploaded_bytes += required_bytes;
    return core::Status::ok();
}

core::Status GpuMaterialTable::shutdown() {
    auto status = core::Status::ok();
    if (buffer_.is_valid()) {
        status = device_.release_resource(buffer_);
    }
    buffer_ = {};
    capacity_bytes_ = 0;
    stats_.material_count = 0;
    stats_.resident_bytes = 0;
    stats_.capacity_bytes = 0;
    stats_.resident_revision = 0;
    return status;
}

rhi::RenderResourceHandle GpuMaterialTable::buffer() const noexcept {
    return buffer_;
}

const GpuMaterialTableStats& GpuMaterialTable::stats() const noexcept {
    return stats_;
}

} // namespace heartstead::renderer
