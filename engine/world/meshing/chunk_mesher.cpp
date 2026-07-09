#include "engine/world/meshing/chunk_mesher.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace heartstead::world {

namespace {

struct FaceTemplate {
    ChunkMeshFaceDirection direction = ChunkMeshFaceDirection::positive_x;
    std::int32_t offset_x = 0;
    std::int32_t offset_y = 0;
    std::int32_t offset_z = 0;
    std::array<math::Vec3f, 4> corners{};
};

[[nodiscard]] constexpr std::array<FaceTemplate, 6> face_templates() noexcept {
    return {
        FaceTemplate{ChunkMeshFaceDirection::negative_x,
                     -1,
                     0,
                     0,
                     {math::Vec3f{0.0F, 0.0F, 1.0F}, math::Vec3f{0.0F, 1.0F, 1.0F},
                      math::Vec3f{0.0F, 1.0F, 0.0F}, math::Vec3f{0.0F, 0.0F, 0.0F}}},
        FaceTemplate{ChunkMeshFaceDirection::positive_x,
                     1,
                     0,
                     0,
                     {math::Vec3f{1.0F, 0.0F, 0.0F}, math::Vec3f{1.0F, 1.0F, 0.0F},
                      math::Vec3f{1.0F, 1.0F, 1.0F}, math::Vec3f{1.0F, 0.0F, 1.0F}}},
        FaceTemplate{ChunkMeshFaceDirection::negative_y,
                     0,
                     -1,
                     0,
                     {math::Vec3f{0.0F, 0.0F, 0.0F}, math::Vec3f{1.0F, 0.0F, 0.0F},
                      math::Vec3f{1.0F, 0.0F, 1.0F}, math::Vec3f{0.0F, 0.0F, 1.0F}}},
        FaceTemplate{ChunkMeshFaceDirection::positive_y,
                     0,
                     1,
                     0,
                     {math::Vec3f{0.0F, 1.0F, 1.0F}, math::Vec3f{1.0F, 1.0F, 1.0F},
                      math::Vec3f{1.0F, 1.0F, 0.0F}, math::Vec3f{0.0F, 1.0F, 0.0F}}},
        FaceTemplate{ChunkMeshFaceDirection::negative_z,
                     0,
                     0,
                     -1,
                     {math::Vec3f{0.0F, 0.0F, 0.0F}, math::Vec3f{0.0F, 1.0F, 0.0F},
                      math::Vec3f{1.0F, 1.0F, 0.0F}, math::Vec3f{1.0F, 0.0F, 0.0F}}},
        FaceTemplate{ChunkMeshFaceDirection::positive_z,
                     0,
                     0,
                     1,
                     {math::Vec3f{1.0F, 0.0F, 1.0F}, math::Vec3f{1.0F, 1.0F, 1.0F},
                      math::Vec3f{0.0F, 1.0F, 1.0F}, math::Vec3f{0.0F, 0.0F, 1.0F}}},
    };
}

[[nodiscard]] bool coord_inside(std::int32_t value) noexcept {
    return value >= 0 && value < static_cast<std::int32_t>(VoxelChunk::edge_length);
}

[[nodiscard]] bool is_neighbor_solid(const VoxelChunk& chunk, std::int32_t x, std::int32_t y,
                                     std::int32_t z) {
    if (!coord_inside(x) || !coord_inside(y) || !coord_inside(z)) {
        return false;
    }
    auto cell = chunk.get({static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                           static_cast<std::uint16_t>(z)});
    return cell && !cell.value().is_air();
}

void expand_bounds(math::Bounds3f& bounds, math::Vec3f point) noexcept {
    bounds.min = math::component_min(bounds.min, point);
    bounds.max = math::component_max(bounds.max, point);
}

void add_face(ChunkMesh& mesh, VoxelCoord coord, VoxelCell cell, const FaceTemplate& face) {
    const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto normal = chunk_mesh_face_normal(face.direction);
    constexpr std::array<std::pair<float, float>, 4> uvs{
        std::pair{0.0F, 0.0F}, std::pair{1.0F, 0.0F}, std::pair{1.0F, 1.0F}, std::pair{0.0F, 1.0F}};

    const math::Vec3f origin{static_cast<float>(coord.x), static_cast<float>(coord.y),
                             static_cast<float>(coord.z)};
    for (std::size_t index = 0; index < face.corners.size(); ++index) {
        const auto position = origin + face.corners[index];
        mesh.vertices.push_back(ChunkMeshVertex{position, normal, uvs[index].first,
                                                uvs[index].second, cell.type, cell.light});
        if (mesh.face_count == 0 && mesh.vertices.size() == 1) {
            mesh.local_bounds = math::Bounds3f{position, position};
        } else {
            expand_bounds(mesh.local_bounds, position);
        }
    }

    mesh.indices.push_back(base_index + 0);
    mesh.indices.push_back(base_index + 1);
    mesh.indices.push_back(base_index + 2);
    mesh.indices.push_back(base_index + 0);
    mesh.indices.push_back(base_index + 2);
    mesh.indices.push_back(base_index + 3);
    ++mesh.face_count;
}

} // namespace

bool ChunkMesh::empty() const noexcept {
    return vertices.empty() && indices.empty() && face_count == 0;
}

core::Status ChunkMesh::validate() const {
    if (vertices.empty() || indices.empty() || face_count == 0) {
        return vertices.empty() && indices.empty() && face_count == 0
                   ? core::Status::ok()
                   : core::Status::failure("chunk_mesh.incomplete",
                                           "chunk mesh has incomplete vertex/index data");
    }
    if (!local_bounds.is_valid()) {
        return core::Status::failure("chunk_mesh.invalid_bounds", "chunk mesh bounds are invalid");
    }
    if (vertices.size() != face_count * 4) {
        return core::Status::failure("chunk_mesh.invalid_vertex_count",
                                     "chunk mesh vertex count does not match face count");
    }
    if (indices.size() != face_count * 6) {
        return core::Status::failure("chunk_mesh.invalid_index_count",
                                     "chunk mesh index count does not match face count");
    }
    if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return core::Status::failure("chunk_mesh.too_many_vertices",
                                     "chunk mesh has too many vertices for uint32 indices");
    }
    for (const auto index : indices) {
        if (index >= vertices.size()) {
            return core::Status::failure("chunk_mesh.index_out_of_range",
                                         "chunk mesh index references a missing vertex");
        }
    }
    for (const auto& vertex : vertices) {
        if (!vertex.position.is_finite() || !vertex.normal.is_finite()) {
            return core::Status::failure("chunk_mesh.invalid_vertex",
                                         "chunk mesh vertex contains non-finite values");
        }
        if (vertex.voxel_type == VoxelCell::air().type) {
            return core::Status::failure("chunk_mesh.air_vertex",
                                         "chunk mesh vertex cannot reference air");
        }
    }
    return core::Status::ok();
}

core::Result<ChunkMesh> ChunkMesher::build_surface_mesh(const VoxelChunk& chunk) {
    ChunkMesh mesh;
    mesh.chunk_coord = chunk.coord();

    for (std::uint16_t z = 0; z < VoxelChunk::edge_length; ++z) {
        for (std::uint16_t y = 0; y < VoxelChunk::edge_length; ++y) {
            for (std::uint16_t x = 0; x < VoxelChunk::edge_length; ++x) {
                const auto cell = chunk.get({x, y, z});
                if (!cell) {
                    return core::Result<ChunkMesh>::failure(cell.error().code,
                                                            cell.error().message);
                }
                if (cell.value().is_air()) {
                    continue;
                }

                for (const auto& face : face_templates()) {
                    if (!is_neighbor_solid(chunk, static_cast<std::int32_t>(x) + face.offset_x,
                                           static_cast<std::int32_t>(y) + face.offset_y,
                                           static_cast<std::int32_t>(z) + face.offset_z)) {
                        add_face(mesh, {x, y, z}, cell.value(), face);
                    }
                }
            }
        }
    }

    auto status = mesh.validate();
    if (!status) {
        return core::Result<ChunkMesh>::failure(status.error().code, status.error().message);
    }
    return core::Result<ChunkMesh>::success(std::move(mesh));
}

math::Vec3f chunk_mesh_face_normal(ChunkMeshFaceDirection direction) noexcept {
    switch (direction) {
    case ChunkMeshFaceDirection::negative_x:
        return {-1.0F, 0.0F, 0.0F};
    case ChunkMeshFaceDirection::positive_x:
        return {1.0F, 0.0F, 0.0F};
    case ChunkMeshFaceDirection::negative_y:
        return {0.0F, -1.0F, 0.0F};
    case ChunkMeshFaceDirection::positive_y:
        return {0.0F, 1.0F, 0.0F};
    case ChunkMeshFaceDirection::negative_z:
        return {0.0F, 0.0F, -1.0F};
    case ChunkMeshFaceDirection::positive_z:
        return {0.0F, 0.0F, 1.0F};
    }
    return {0.0F, 1.0F, 0.0F};
}

} // namespace heartstead::world
