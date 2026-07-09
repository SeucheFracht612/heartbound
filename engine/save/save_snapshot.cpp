#include "engine/save/save_snapshot.hpp"

#include "engine/workpieces/workpiece_codec.hpp"
#include "engine/world/chunks/chunk_edit_delta_codec.hpp"

#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::save {

namespace {

void add_issue(SaveSnapshotValidation& validation, std::string code, std::string message) {
    validation.issues.push_back(SaveSnapshotIssue{std::move(code), std::move(message)});
}

void add_status_issue(SaveSnapshotValidation& validation, const core::Status& status) {
    if (!status) {
        add_issue(validation, status.error().code, status.error().message);
    }
}

void validate_save_id_unique(SaveSnapshotValidation& validation, std::set<std::uint64_t>& ids,
                             core::SaveId id, std::string_view label) {
    if (!id.is_valid()) {
        add_issue(validation, "save_snapshot.invalid_save_id",
                  std::string(label) + " has an invalid save id");
        return;
    }
    if (!ids.insert(id.value()).second) {
        add_issue(validation, "save_snapshot.duplicate_save_id",
                  std::string(label) + " duplicates save id " + id.to_string());
    }
}

void require_kind(SaveSnapshotValidation& validation, const modding::PrototypeRegistry& prototypes,
                  const core::PrototypeId& id, std::string_view kind) {
    auto status = prototypes.require_kind(id, kind);
    add_status_issue(validation, status);
}

void require_reference(SaveSnapshotValidation& validation,
                       const modding::PrototypeRegistry& prototypes, const core::PrototypeId& id) {
    auto status = prototypes.require(id);
    add_status_issue(validation, status);
}

} // namespace

bool SaveSnapshotValidation::valid() const noexcept {
    return issues.empty();
}

core::Status EntitySaveRecord::validate() const {
    if (!save_id.is_valid()) {
        return core::Status::failure("save_snapshot.invalid_entity_id",
                                     "entity save record needs a stable save id");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("save_snapshot.invalid_entity_prototype",
                                     "entity save record prototype id must be valid");
    }
    if (!entities::is_known_entity_kind(kind)) {
        return core::Status::failure("save_snapshot.invalid_entity_kind",
                                     "entity save record kind is unknown");
    }
    if (!transform.is_finite()) {
        return core::Status::failure("save_snapshot.invalid_entity_transform",
                                     "entity save record transform must contain finite values");
    }
    if (!transform.has_non_zero_scale()) {
        return core::Status::failure("save_snapshot.invalid_entity_transform_scale",
                                     "entity save record transform scale must be non-zero");
    }
    return core::Status::ok();
}

SaveSnapshotValidation
SaveSnapshotValidator::validate(const SaveSnapshot& snapshot,
                                const modding::PrototypeRegistry& prototypes) {
    SaveSnapshotValidation validation;
    std::set<std::uint64_t> save_ids;
    std::set<std::uint64_t> build_piece_ids;
    std::set<std::uint64_t> inventory_owner_ids;
    std::set<std::uint64_t> workpiece_ids;
    std::set<std::uint64_t> process_ids;
    std::set<world::ChunkCoord> chunk_edit_coords;
    std::set<std::pair<std::string, std::string>> mod_state_keys;
    std::vector<std::pair<std::string, core::SaveId>> owner_refs;

    add_status_issue(validation, snapshot.metadata.validate());

    for (const auto& chunk : snapshot.chunk_edits) {
        if (!chunk_edit_coords.insert(chunk.coord).second) {
            add_issue(validation, "save_snapshot.duplicate_chunk_edit",
                      "duplicate chunk edit record for chunk coordinate");
        }
        if (chunk.encoded_edit_delta.empty()) {
            add_issue(validation, "save_snapshot.empty_chunk_delta",
                      "chunk edit delta payload must not be empty");
        } else {
            auto edits =
                world::ChunkEditDeltaTextCodec::decode(chunk.coord, chunk.encoded_edit_delta);
            if (!edits) {
                add_issue(validation, edits.error().code, edits.error().message);
            }
        }
    }

    for (const auto& build_piece : snapshot.build_pieces) {
        add_status_issue(validation, build_piece.validate());
        validate_save_id_unique(validation, save_ids, build_piece.object_id, "build piece");
        if (build_piece.object_id.is_valid()) {
            build_piece_ids.insert(build_piece.object_id.value());
        }
        require_kind(validation, prototypes, build_piece.prototype_id,
                     modding::PrototypeKinds::build_piece);
    }

    for (const auto& entity : snapshot.entities) {
        add_status_issue(validation, entity.validate());
        validate_save_id_unique(validation, save_ids, entity.save_id, "entity");
        require_kind(validation, prototypes, entity.prototype_id, modding::PrototypeKinds::entity);
    }

    for (const auto& inventory : snapshot.inventories) {
        if (!inventory.owner_id.is_valid()) {
            add_issue(validation, "save_snapshot.invalid_inventory_owner",
                      "inventory owner save id must be valid");
        } else {
            if (!inventory_owner_ids.insert(inventory.owner_id.value()).second) {
                add_issue(validation, "save_snapshot.duplicate_inventory_owner",
                          "duplicate inventory owner " + inventory.owner_id.to_string());
            }
            owner_refs.emplace_back("inventory", inventory.owner_id);
        }
        for (const auto& stack : inventory.stacks) {
            if (stack.is_empty()) {
                add_issue(validation, "save_snapshot.empty_item_stack",
                          "saved inventories must not contain empty item stacks");
            }
            if (stack.max_count == 0 || stack.count > stack.max_count) {
                add_issue(validation, "save_snapshot.invalid_item_stack_count",
                          "saved item stack count must be between 1 and max count");
            }
            require_kind(validation, prototypes, stack.prototype_id, modding::PrototypeKinds::item);
        }
    }

    for (const auto& cargo_record : snapshot.cargo_records) {
        add_status_issue(validation, cargo_record.validate());
        validate_save_id_unique(validation, save_ids, cargo_record.cargo_id, "cargo");
        require_kind(validation, prototypes, cargo_record.prototype_id,
                     modding::PrototypeKinds::cargo);
    }

    for (const auto& workpiece : snapshot.workpieces) {
        if (!workpiece.workpiece_id.is_valid()) {
            add_issue(validation, "save_snapshot.invalid_workpiece_id",
                      "workpiece save record needs a stable workpiece id");
        } else if (!workpiece_ids.insert(workpiece.workpiece_id.value()).second) {
            add_issue(validation, "save_snapshot.duplicate_workpiece_id",
                      "duplicate workpiece id " + workpiece.workpiece_id.to_string());
        }
        if (workpiece.encoded_cells.empty()) {
            add_issue(validation, "save_snapshot.empty_workpiece_cells",
                      "workpiece cell payload must not be empty");
        } else {
            auto grid = workpieces::WorkpieceGridTextCodec::decode(workpiece.encoded_cells);
            if (!grid) {
                add_issue(validation, grid.error().code, grid.error().message);
            } else if (grid.value().shape() != workpiece.shape) {
                add_issue(validation, "save_snapshot.workpiece_shape_mismatch",
                          "decoded workpiece grid shape does not match save record shape");
            }
        }
        require_kind(validation, prototypes, workpiece.prototype_id,
                     modding::PrototypeKinds::workpiece);
    }

    for (const auto& assembly : snapshot.assemblies) {
        add_status_issue(validation, assembly.validate_record());
        validate_save_id_unique(validation, save_ids, assembly.assembly_id, "assembly");
        require_kind(validation, prototypes, assembly.prototype_id,
                     modding::PrototypeKinds::assembly);
        if (!build_piece_ids.contains(assembly.root_build_piece_id.value())) {
            add_issue(validation, "save_snapshot.missing_assembly_root",
                      "assembly root build piece is not present in saved build pieces");
        }
        for (const auto& part : assembly.parts) {
            if (part.name.empty()) {
                add_issue(validation, "save_snapshot.empty_assembly_part_name",
                          "assembly part name must not be empty");
            }
            if (!part.build_piece_id.is_valid() ||
                !build_piece_ids.contains(part.build_piece_id.value())) {
                add_issue(validation, "save_snapshot.missing_assembly_part",
                          "assembly part references a missing build piece");
            }
            require_kind(validation, prototypes, part.prototype_id,
                         modding::PrototypeKinds::build_piece);
        }
        for (const auto& port : assembly.ports) {
            if (!port.source_build_piece_id.is_valid() ||
                !build_piece_ids.contains(port.source_build_piece_id.value())) {
                add_issue(validation, "save_snapshot.missing_assembly_port_source",
                          "assembly port source build piece is not present in saved build pieces");
            }
        }
    }

    for (const auto& process : snapshot.processes) {
        add_status_issue(validation, process.validate());
        if (!process.process_id.is_valid()) {
            add_issue(validation, "save_snapshot.invalid_process_id",
                      "process save record needs a stable process id");
        } else if (!process_ids.insert(process.process_id.value()).second) {
            add_issue(validation, "save_snapshot.duplicate_process_id",
                      "duplicate process id " + process.process_id.to_string());
        }
        if (!process.owner_id.is_valid()) {
            add_issue(validation, "save_snapshot.invalid_process_owner",
                      "process owner save id must be valid");
        } else {
            owner_refs.emplace_back("process", process.owner_id);
        }
        require_kind(validation, prototypes, process.prototype_id,
                     modding::PrototypeKinds::process);
        for (const auto& slot : process.input_slots) {
            if (slot.prototype_id.is_valid()) {
                require_reference(validation, prototypes, slot.prototype_id);
            }
        }
        for (const auto& slot : process.output_slots) {
            if (slot.prototype_id.is_valid()) {
                require_reference(validation, prototypes, slot.prototype_id);
            }
        }
    }

    for (const auto& mod_state : snapshot.mod_states) {
        if (!core::is_valid_namespace_id(mod_state.mod_id)) {
            add_issue(validation, "save_snapshot.invalid_mod_state_id",
                      "mod state record has an invalid mod id");
        }
        if (mod_state.state_key.empty()) {
            add_issue(validation, "save_snapshot.empty_mod_state_key",
                      "mod state record needs a state key");
        } else if (!mod_state_keys.insert({mod_state.mod_id, mod_state.state_key}).second) {
            add_issue(validation, "save_snapshot.duplicate_mod_state",
                      "duplicate mod state key " + mod_state.mod_id + ":" + mod_state.state_key);
        }
    }

    for (const auto& [label, owner_id] : owner_refs) {
        if (!save_ids.contains(owner_id.value())) {
            add_issue(validation, "save_snapshot.missing_owner",
                      label + " references a missing owner save id " + owner_id.to_string());
        }
    }

    return validation;
}

} // namespace heartstead::save
