#pragma once

#include "engine/assemblies/assembly.hpp"
#include "engine/build/build_piece.hpp"
#include "engine/cargo/cargo.hpp"
#include "engine/core/ids.hpp"
#include "engine/entities/entity.hpp"
#include "engine/items/item_stack.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/processes/process.hpp"
#include "engine/save/save_metadata.hpp"
#include "engine/simulation/fire.hpp"
#include "engine/workpieces/workpiece_grid.hpp"
#include "engine/world/missing_prototype.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"
#include "engine/world/voxels/voxel_palette.hpp"

#include <string>
#include <utility>
#include <vector>

namespace heartstead::save {

struct ChunkEditSaveRecord {
    world::ChunkCoord coord;
    std::string encoded_edit_delta;
};

struct InventorySaveRecord {
    core::SaveId owner_id;
    std::vector<items::ItemStack> stacks;
};

struct EntitySaveRecord {
    core::SaveId save_id;
    core::PrototypeId prototype_id;
    entities::EntityKind kind = entities::EntityKind::temporary_physics;
    bool sleeping = false;
    std::string encoded_state;
    entities::Transform transform;

    [[nodiscard]] core::Status validate() const;
};

struct WorkpieceSaveRecord {
    core::WorkpieceId workpiece_id;
    core::PrototypeId prototype_id;
    workpieces::WorkpieceGridShape shape;
    std::string encoded_cells;
    core::PrototypeId material_prototype_id;
    std::string encoded_server_state;
    core::NetId owner_session;
    std::uint64_t revision = 1;
    bool committed = false;

    WorkpieceSaveRecord() = default;
    WorkpieceSaveRecord(core::WorkpieceId id, core::PrototypeId prototype,
                        workpieces::WorkpieceGridShape grid_shape, std::string cells)
        : workpiece_id(id), prototype_id(std::move(prototype)), shape(grid_shape),
          encoded_cells(std::move(cells)) {}
    WorkpieceSaveRecord(core::WorkpieceId id, core::PrototypeId prototype,
                        workpieces::WorkpieceGridShape grid_shape, std::string cells,
                        core::PrototypeId material, std::string server_state, core::NetId owner,
                        std::uint64_t record_revision, bool is_committed)
        : workpiece_id(id), prototype_id(std::move(prototype)), shape(grid_shape),
          encoded_cells(std::move(cells)), material_prototype_id(std::move(material)),
          encoded_server_state(std::move(server_state)), owner_session(owner),
          revision(record_revision), committed(is_committed) {}
};

struct ModStateSaveRecord {
    std::string mod_id;
    std::string state_key;
    std::string encoded_state;
};

struct SaveSnapshot {
    SaveMetadata metadata;
    world::VoxelPaletteManifest voxel_palette;
    std::vector<ChunkEditSaveRecord> chunk_edits;
    std::vector<build::BuildPieceRecord> build_pieces;
    std::vector<EntitySaveRecord> entities;
    std::vector<InventorySaveRecord> inventories;
    std::vector<cargo::CargoRecord> cargo_records;
    std::vector<WorkpieceSaveRecord> workpieces;
    std::vector<assemblies::AssemblyRecord> assemblies;
    std::vector<processes::ProcessInstance> processes;
    std::vector<ModStateSaveRecord> mod_states;
    std::vector<world::MissingPrototypeObject> missing_prototypes;
    std::vector<simulation::FireInstance> fires;
};

struct SaveSnapshotIssue {
    std::string code;
    std::string message;
};

struct SaveSnapshotValidation {
    std::vector<SaveSnapshotIssue> issues;

    [[nodiscard]] bool valid() const noexcept;
};

class SaveSnapshotValidator {
  public:
    [[nodiscard]] static SaveSnapshotValidation
    validate(const SaveSnapshot& snapshot, const modding::PrototypeRegistry& prototypes);
};

} // namespace heartstead::save
