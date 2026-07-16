#include "engine/world/meshing/greedy_chunk_mesher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace heartstead::world {

namespace {

constexpr auto edge = static_cast<std::uint16_t>(VoxelChunk::edge_length);
constexpr auto mask_cell_count = static_cast<std::size_t>(edge) * edge;

struct GreedyFaceKey {
    std::uint16_t voxel_type = 0;
    std::uint16_t material_index = 0;
    std::uint16_t state_bits = 0;
    std::uint16_t block_flags = 0;
    std::uint8_t light = 0;
    MeshingRenderPhase render_phase = MeshingRenderPhase::opaque;

    friend bool operator==(const GreedyFaceKey&, const GreedyFaceKey&) = default;
};

struct MaskCell {
    GreedyFaceKey key{};
    bool present = false;
};

[[nodiscard]] constexpr std::uint8_t face_bit(ChunkMeshFaceDirection direction) noexcept {
    return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(direction));
}

[[nodiscard]] constexpr ChunkMeshFaceDirection opposite(ChunkMeshFaceDirection direction) noexcept {
    switch (direction) {
    case ChunkMeshFaceDirection::negative_x:
        return ChunkMeshFaceDirection::positive_x;
    case ChunkMeshFaceDirection::positive_x:
        return ChunkMeshFaceDirection::negative_x;
    case ChunkMeshFaceDirection::negative_y:
        return ChunkMeshFaceDirection::positive_y;
    case ChunkMeshFaceDirection::positive_y:
        return ChunkMeshFaceDirection::negative_y;
    case ChunkMeshFaceDirection::negative_z:
        return ChunkMeshFaceDirection::positive_z;
    case ChunkMeshFaceDirection::positive_z:
        return ChunkMeshFaceDirection::negative_z;
    }
    return ChunkMeshFaceDirection::positive_y;
}

struct CellAddress {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

[[nodiscard]] constexpr CellAddress address(ChunkMeshFaceDirection direction, std::uint16_t slice,
                                            std::uint16_t u, std::uint16_t v) noexcept {
    switch (direction) {
    case ChunkMeshFaceDirection::negative_x:
    case ChunkMeshFaceDirection::positive_x:
        return {slice, u, v};
    case ChunkMeshFaceDirection::negative_y:
    case ChunkMeshFaceDirection::positive_y:
        return {u, slice, v};
    case ChunkMeshFaceDirection::negative_z:
    case ChunkMeshFaceDirection::positive_z:
        return {u, v, slice};
    }
    return {};
}

[[nodiscard]] constexpr CellAddress neighbor_address(CellAddress source,
                                                     ChunkMeshFaceDirection direction) noexcept {
    switch (direction) {
    case ChunkMeshFaceDirection::negative_x:
        --source.x;
        break;
    case ChunkMeshFaceDirection::positive_x:
        ++source.x;
        break;
    case ChunkMeshFaceDirection::negative_y:
        --source.y;
        break;
    case ChunkMeshFaceDirection::positive_y:
        ++source.y;
        break;
    case ChunkMeshFaceDirection::negative_z:
        --source.z;
        break;
    case ChunkMeshFaceDirection::positive_z:
        ++source.z;
        break;
    }
    return source;
}

[[nodiscard]] bool face_is_visible(const ChunkNeighborhoodSnapshot& neighborhood,
                                   const BlockRenderTableSnapshot& render_table, CellAddress source,
                                   ChunkMeshFaceDirection direction) noexcept {
    const auto neighbor = neighbor_address(source, direction);
    const auto neighbor_cell = neighborhood.cell_relative(neighbor.x, neighbor.y, neighbor.z);
    if (neighbor_cell.is_air()) {
        return true;
    }
    const auto* neighbor_block = render_table.find(neighbor_cell.type);
    return neighbor_block == nullptr ||
           (neighbor_block->occlusion_mask & face_bit(opposite(direction))) == 0;
}

void include_point(ChunkMesh& mesh, math::Vec3f point) noexcept {
    if (mesh.vertices.empty()) {
        mesh.local_bounds = {point, point};
        return;
    }
    mesh.local_bounds.min = math::component_min(mesh.local_bounds.min, point);
    mesh.local_bounds.max = math::component_max(mesh.local_bounds.max, point);
}

void emit_quad(ChunkMesh& mesh, ChunkMeshFaceDirection direction, std::uint16_t slice,
               std::uint16_t u, std::uint16_t v, std::uint16_t width, std::uint16_t height,
               const GreedyFaceKey& key) {
    const auto plane_min = static_cast<float>(slice);
    const auto plane_max = static_cast<float>(slice + 1U);
    const auto u0 = static_cast<float>(u);
    const auto u1 = static_cast<float>(u + width);
    const auto v0 = static_cast<float>(v);
    const auto v1 = static_cast<float>(v + height);
    std::array<math::Vec3f, 4> positions{};
    switch (direction) {
    case ChunkMeshFaceDirection::negative_x:
        positions = {
            {{plane_min, u0, v1}, {plane_min, u1, v1}, {plane_min, u1, v0}, {plane_min, u0, v0}}};
        break;
    case ChunkMeshFaceDirection::positive_x:
        positions = {
            {{plane_max, u0, v0}, {plane_max, u1, v0}, {plane_max, u1, v1}, {plane_max, u0, v1}}};
        break;
    case ChunkMeshFaceDirection::negative_y:
        positions = {
            {{u0, plane_min, v0}, {u1, plane_min, v0}, {u1, plane_min, v1}, {u0, plane_min, v1}}};
        break;
    case ChunkMeshFaceDirection::positive_y:
        positions = {
            {{u0, plane_max, v1}, {u1, plane_max, v1}, {u1, plane_max, v0}, {u0, plane_max, v0}}};
        break;
    case ChunkMeshFaceDirection::negative_z:
        positions = {
            {{u0, v0, plane_min}, {u0, v1, plane_min}, {u1, v1, plane_min}, {u1, v0, plane_min}}};
        break;
    case ChunkMeshFaceDirection::positive_z:
        positions = {
            {{u1, v0, plane_max}, {u1, v1, plane_max}, {u0, v1, plane_max}, {u0, v0, plane_max}}};
        break;
    }

    const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(mesh.indices.size());
    const std::array<std::pair<float, float>, 4> uvs{
        std::pair{0.0F, 0.0F}, std::pair{static_cast<float>(width), 0.0F},
        std::pair{static_cast<float>(width), static_cast<float>(height)},
        std::pair{0.0F, static_cast<float>(height)}};
    const auto normal = chunk_mesh_face_normal(direction);
    for (std::size_t index = 0; index < positions.size(); ++index) {
        include_point(mesh, positions[index]);
        mesh.vertices.push_back({positions[index], normal, uvs[index].first, uvs[index].second,
                                 key.voxel_type, key.light, key.state_bits});
    }
    mesh.indices.insert(mesh.indices.end(), {base_index, base_index + 1U, base_index + 2U,
                                             base_index, base_index + 2U, base_index + 3U});
    if (!mesh.sections.empty() && mesh.sections.back().material_index == key.material_index &&
        mesh.sections.back().render_phase == key.render_phase &&
        mesh.sections.back().first_index + mesh.sections.back().index_count == first_index) {
        mesh.sections.back().index_count += 6;
    } else {
        mesh.sections.push_back({key.material_index, key.render_phase, first_index, 6});
    }
    ++mesh.face_count;
}

[[nodiscard]] core::Result<std::size_t>
validate_and_count_cube_cells(const ChunkNeighborhoodSnapshot& neighborhood,
                              const BlockRenderTableSnapshot& render_table) {
    std::size_t non_air_count = 0;
    for (std::uint16_t z = 0; z < edge; ++z) {
        for (std::uint16_t y = 0; y < edge; ++y) {
            for (std::uint16_t x = 0; x < edge; ++x) {
                const auto cell = neighborhood.cell(x, y, z);
                if (cell.is_air()) {
                    continue;
                }
                ++non_air_count;
                const auto* block = render_table.find(cell.type);
                if (block == nullptr) {
                    return core::Result<std::size_t>::failure(
                        "chunk_mesh.unknown_voxel_type",
                        "snapshot contains a voxel type missing from its block render table");
                }
                if (block->geometry != MeshingGeometryKind::full_cube) {
                    return core::Result<std::size_t>::success(0);
                }
            }
        }
    }
    return core::Result<std::size_t>::success(non_air_count);
}

} // namespace

core::Result<ChunkMesh>
GreedyChunkMesher::build_surface_mesh(const ChunkNeighborhoodSnapshot& neighborhood,
                                      const BlockRenderTableSnapshot& render_table) {
    auto status = neighborhood.validate();
    if (!status) {
        return core::Result<ChunkMesh>::failure(status.error().code, status.error().message);
    }
    status = render_table.validate();
    if (!status) {
        return core::Result<ChunkMesh>::failure(status.error().code, status.error().message);
    }
    auto cube_cells = validate_and_count_cube_cells(neighborhood, render_table);
    if (!cube_cells) {
        return core::Result<ChunkMesh>::failure(cube_cells.error().code,
                                                cube_cells.error().message);
    }
    if (cube_cells.value() == 0) {
        // Specialized geometry remains on independent, reference-checked emitters while the full
        // cube hot loop is optimized. Empty chunks also take this harmless path.
        return ChunkMesher::build_surface_mesh(neighborhood, render_table);
    }

    ChunkMesh mesh;
    mesh.chunk_coord = neighborhood.center_identity.coordinate;
    mesh.provided_halo_radius = neighborhood.halo_radius;
    mesh.required_halo_radius = neighborhood.halo_radius;
    const auto reserve_quads = std::min<std::size_t>(cube_cells.value() * 2U, 16U * 1024U);
    mesh.vertices.reserve(reserve_quads * 4U);
    mesh.indices.reserve(reserve_quads * 6U);

    std::array<MaskCell, mask_cell_count> mask{};
    constexpr std::array directions{
        ChunkMeshFaceDirection::negative_x, ChunkMeshFaceDirection::positive_x,
        ChunkMeshFaceDirection::negative_y, ChunkMeshFaceDirection::positive_y,
        ChunkMeshFaceDirection::negative_z, ChunkMeshFaceDirection::positive_z,
    };
    for (const auto direction : directions) {
        for (std::uint16_t slice = 0; slice < edge; ++slice) {
            std::ranges::fill(mask, MaskCell{});
            for (std::uint16_t v = 0; v < edge; ++v) {
                for (std::uint16_t u = 0; u < edge; ++u) {
                    const auto source = address(direction, slice, u, v);
                    const auto cell = neighborhood.cell_relative(source.x, source.y, source.z);
                    if (cell.is_air() ||
                        !face_is_visible(neighborhood, render_table, source, direction)) {
                        continue;
                    }
                    const auto* block = render_table.find(cell.type);
                    auto& item = mask[static_cast<std::size_t>(v) * edge + u];
                    item.present = true;
                    item.key = {cell.type,
                                block->material_index == 0 ? cell.type : block->material_index,
                                cell.state_bits,
                                block->flags,
                                cell.light,
                                block->render_phase};
                }
            }

            for (std::uint16_t v = 0; v < edge; ++v) {
                for (std::uint16_t u = 0; u < edge;) {
                    auto& first = mask[static_cast<std::size_t>(v) * edge + u];
                    if (!first.present) {
                        ++u;
                        continue;
                    }
                    std::uint16_t width = 1;
                    while (u + width < edge) {
                        const auto& candidate =
                            mask[static_cast<std::size_t>(v) * edge + u + width];
                        if (!candidate.present || candidate.key != first.key) {
                            break;
                        }
                        ++width;
                    }
                    std::uint16_t height = 1;
                    for (; v + height < edge; ++height) {
                        bool compatible = true;
                        for (std::uint16_t offset = 0; offset < width; ++offset) {
                            const auto& candidate =
                                mask[static_cast<std::size_t>(v + height) * edge + u + offset];
                            if (!candidate.present || candidate.key != first.key) {
                                compatible = false;
                                break;
                            }
                        }
                        if (!compatible) {
                            break;
                        }
                    }
                    const auto key = first.key;
                    for (std::uint16_t row = 0; row < height; ++row) {
                        for (std::uint16_t column = 0; column < width; ++column) {
                            mask[static_cast<std::size_t>(v + row) * edge + u + column].present =
                                false;
                        }
                    }
                    emit_quad(mesh, direction, slice, u, v, width, height, key);
                    u = static_cast<std::uint16_t>(u + width);
                }
            }
        }
    }

    status = mesh.finalize_sections();
    if (!status) {
        return core::Result<ChunkMesh>::failure(status.error().code, status.error().message);
    }
    status = mesh.validate();
    if (!status) {
        return core::Result<ChunkMesh>::failure(status.error().code, status.error().message);
    }
    return core::Result<ChunkMesh>::success(std::move(mesh));
}

} // namespace heartstead::world
