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
#include "engine/workpieces/workpiece_grid.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <string>
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
};

struct ModStateSaveRecord {
    std::string mod_id;
    std::string state_key;
    std::string encoded_state;
};

struct SaveSnapshot {
    SaveMetadata metadata;
    std::vector<ChunkEditSaveRecord> chunk_edits;
    std::vector<build::BuildPieceRecord> build_pieces;
    std::vector<EntitySaveRecord> entities;
    std::vector<InventorySaveRecord> inventories;
    std::vector<cargo::CargoRecord> cargo_records;
    std::vector<WorkpieceSaveRecord> workpieces;
    std::vector<assemblies::AssemblyRecord> assemblies;
    std::vector<processes::ProcessInstance> processes;
    std::vector<ModStateSaveRecord> mod_states;
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
