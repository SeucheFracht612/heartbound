#include "engine/save/missing_prototype_recovery.hpp"

#include "engine/entities/physical_resource.hpp"
#include "engine/save/save_text_codec.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace heartstead::save {

namespace {

[[nodiscard]] bool prototype_missing(const modding::PrototypeRegistry& prototypes,
                                     const core::PrototypeId& id) {
    // A present prototype with the wrong semantic kind is corrupted content, not missing content.
    // Leave that record intact so SaveSnapshotValidator can report the kind mismatch.
    return prototypes.find(id) == nullptr;
}

template <typename AddRecord>
[[nodiscard]] std::string record_blob(const SaveSnapshot& source, AddRecord add_record) {
    SaveSnapshot single;
    single.metadata = source.metadata;
    single.voxel_palette = source.voxel_palette;
    add_record(single);
    return SaveTextCodec::encode_snapshot(single);
}

using MissingPlaceholderKey = std::pair<world::MissingPrototypeKind, std::uint64_t>;

void add_placeholder(SaveSnapshot& snapshot, std::set<MissingPlaceholderKey>& placeholder_keys,
                     std::optional<core::Error>& placeholder_error,
                     world::MissingPrototypeKind kind, std::uint64_t stable_id,
                     const core::PrototypeId& prototype_id, world::WorldPosition position,
                     core::SaveId owner_id, std::string blob, std::string warning) {
    if (!placeholder_keys.emplace(kind, stable_id).second) {
        if (!placeholder_error.has_value()) {
            placeholder_error =
                core::Error{"save_recovery.placeholder_conflict",
                            "missing-prototype placeholder conflicts with another saved record"};
        }
        return;
    }
    snapshot.missing_prototypes.push_back(
        {kind, stable_id, prototype_id, position, owner_id, std::move(blob), std::move(warning)});
}

} // namespace

core::Result<MissingPrototypeRecoveryReport>
preserve_missing_prototypes_in_place(SaveSnapshot& snapshot,
                                     const modding::PrototypeRegistry& prototypes) {
    MissingPrototypeRecoveryReport report;
    std::set<MissingPlaceholderKey> placeholder_keys;
    std::optional<core::Error> placeholder_error;
    for (const auto& missing : snapshot.missing_prototypes) {
        auto status = missing.validate();
        if (!status) {
            return core::Result<MissingPrototypeRecoveryReport>::failure(status.error().code,
                                                                         status.error().message);
        }
        if (!placeholder_keys.emplace(missing.kind, missing.stable_id).second) {
            return core::Result<MissingPrototypeRecoveryReport>::failure(
                "save_recovery.duplicate_placeholder",
                "missing-prototype recovery snapshot contains a duplicate placeholder key");
        }
    }
    std::set<std::uint64_t> missing_owner_ids;
    std::map<std::uint64_t, core::PrototypeId> missing_owner_prototypes;
    std::map<std::uint64_t, world::WorldPosition> build_positions;
    for (const auto& build : snapshot.build_pieces) {
        build_positions.emplace(build.object_id.value(), build.transform.position);
        if (!prototype_missing(prototypes, build.prototype_id)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::build_piece,
            build.object_id.value(), build.prototype_id, build.transform.position, {},
            record_blob(snapshot,
                        [&build](SaveSnapshot& single) { single.build_pieces.push_back(build); }),
            "build piece prototype is unavailable");
        missing_owner_ids.insert(build.object_id.value());
        missing_owner_prototypes.emplace(build.object_id.value(), build.prototype_id);
    }
    std::erase_if(snapshot.build_pieces, [&missing_owner_ids](const auto& record) {
        return missing_owner_ids.contains(record.object_id.value());
    });

    std::set<std::uint64_t> missing_entity_ids;
    for (const auto& entity : snapshot.entities) {
        std::optional<core::PrototypeId> missing_dependency;
        if (entity.encoded_state.starts_with(entities::physical_resource_state_magic)) {
            const auto resource = entities::PhysicalResourceTextCodec::decode(
                entity.save_id, entity.prototype_id, entity.transform.position,
                entity.encoded_state);
            if (resource && prototype_missing(prototypes, resource.value().cargo_prototype_id)) {
                missing_dependency = resource.value().cargo_prototype_id;
            }
        }
        const auto entity_prototype_missing = prototype_missing(prototypes, entity.prototype_id);
        if (!entity_prototype_missing && !missing_dependency.has_value()) {
            continue;
        }
        const auto& unavailable_prototype =
            entity_prototype_missing ? entity.prototype_id : *missing_dependency;
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::entity,
            entity.save_id.value(), unavailable_prototype, entity.transform.position, {},
            record_blob(snapshot,
                        [&entity](SaveSnapshot& single) { single.entities.push_back(entity); }),
            entity_prototype_missing ? "entity prototype is unavailable"
                                     : "entity state references an unavailable cargo prototype");
        missing_entity_ids.insert(entity.save_id.value());
        missing_owner_ids.insert(entity.save_id.value());
        missing_owner_prototypes.emplace(entity.save_id.value(), entity.prototype_id);
    }
    std::erase_if(snapshot.entities, [&missing_entity_ids](const auto& record) {
        return missing_entity_ids.contains(record.save_id.value());
    });

    std::set<std::uint64_t> missing_cargo_ids;
    for (const auto& cargo : snapshot.cargo_records) {
        if (!prototype_missing(prototypes, cargo.prototype_id)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::cargo,
            cargo.cargo_id.value(), cargo.prototype_id, cargo.position, {},
            record_blob(snapshot,
                        [&cargo](SaveSnapshot& single) { single.cargo_records.push_back(cargo); }),
            "cargo prototype is unavailable");
        missing_cargo_ids.insert(cargo.cargo_id.value());
        missing_owner_ids.insert(cargo.cargo_id.value());
        missing_owner_prototypes.emplace(cargo.cargo_id.value(), cargo.prototype_id);
    }
    std::erase_if(snapshot.cargo_records, [&missing_cargo_ids](const auto& record) {
        return missing_cargo_ids.contains(record.cargo_id.value());
    });

    std::set<std::uint64_t> missing_workpiece_ids;
    for (const auto& workpiece : snapshot.workpieces) {
        const auto workpiece_prototype_missing =
            prototype_missing(prototypes, workpiece.prototype_id);
        const auto material_prototype_missing =
            !workpiece.encoded_server_state.empty() &&
            prototype_missing(prototypes, workpiece.material_prototype_id);
        if (!workpiece_prototype_missing && !material_prototype_missing) {
            continue;
        }
        const auto& unavailable_prototype =
            workpiece_prototype_missing ? workpiece.prototype_id : workpiece.material_prototype_id;
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::workpiece,
            workpiece.workpiece_id.value(), unavailable_prototype, {}, {},
            record_blob(
                snapshot,
                [&workpiece](SaveSnapshot& single) { single.workpieces.push_back(workpiece); }),
            workpiece_prototype_missing ? "workpiece prototype is unavailable"
                                        : "workpiece material prototype is unavailable");
        missing_workpiece_ids.insert(workpiece.workpiece_id.value());
        missing_owner_ids.insert(workpiece.workpiece_id.value());
        missing_owner_prototypes.emplace(workpiece.workpiece_id.value(), workpiece.prototype_id);
    }
    std::erase_if(snapshot.workpieces, [&missing_workpiece_ids](const auto& record) {
        return missing_workpiece_ids.contains(record.workpiece_id.value());
    });

    std::set<std::uint64_t> missing_assembly_ids;
    for (const auto& assembly : snapshot.assemblies) {
        bool dependent_missing = missing_owner_ids.contains(assembly.root_build_piece_id.value());
        const core::PrototypeId* missing_part_prototype = nullptr;
        for (const auto& part : assembly.parts) {
            dependent_missing =
                dependent_missing || missing_owner_ids.contains(part.build_piece_id.value());
            if (missing_part_prototype == nullptr &&
                prototype_missing(prototypes, part.prototype_id)) {
                missing_part_prototype = &part.prototype_id;
            }
        }
        const auto assembly_prototype_missing =
            prototype_missing(prototypes, assembly.prototype_id);
        if (!dependent_missing && !assembly_prototype_missing &&
            missing_part_prototype == nullptr) {
            continue;
        }
        const auto position = build_positions.contains(assembly.root_build_piece_id.value())
                                  ? build_positions.at(assembly.root_build_piece_id.value())
                                  : world::WorldPosition{};
        const auto& unavailable_prototype = assembly_prototype_missing ? assembly.prototype_id
                                            : missing_part_prototype != nullptr
                                                ? *missing_part_prototype
                                                : assembly.prototype_id;
        add_placeholder(snapshot, placeholder_keys, placeholder_error,
                        world::MissingPrototypeKind::assembly, assembly.assembly_id.value(),
                        unavailable_prototype, position, assembly.root_build_piece_id,
                        record_blob(snapshot,
                                    [&assembly](SaveSnapshot& single) {
                                        single.assemblies.push_back(assembly);
                                    }),
                        dependent_missing ? "assembly depends on an unavailable saved part"
                        : assembly_prototype_missing ? "assembly prototype is unavailable"
                                                     : "assembly part prototype is unavailable");
        missing_assembly_ids.insert(assembly.assembly_id.value());
        missing_owner_ids.insert(assembly.assembly_id.value());
        missing_owner_prototypes.emplace(assembly.assembly_id.value(), assembly.prototype_id);
        if (dependent_missing)
            ++report.dependent_record_count;
    }
    std::erase_if(snapshot.assemblies, [&missing_assembly_ids](const auto& record) {
        return missing_assembly_ids.contains(record.assembly_id.value());
    });

    std::set<std::uint64_t> missing_inventory_owners;
    for (const auto& inventory : snapshot.inventories) {
        const items::ItemStack* missing_stack = nullptr;
        for (const auto& stack : inventory.stacks) {
            if (prototype_missing(prototypes, stack.prototype_id)) {
                missing_stack = &stack;
                break;
            }
        }
        if (missing_stack == nullptr && !missing_owner_ids.contains(inventory.owner_id.value())) {
            continue;
        }
        const auto prototype = missing_stack != nullptr
                                   ? missing_stack->prototype_id
                                   : missing_owner_prototypes.at(inventory.owner_id.value());
        add_placeholder(snapshot, placeholder_keys, placeholder_error,
                        world::MissingPrototypeKind::inventory, inventory.owner_id.value(),
                        prototype, {}, inventory.owner_id,
                        record_blob(snapshot,
                                    [&inventory](SaveSnapshot& single) {
                                        single.inventories.push_back(inventory);
                                    }),
                        "inventory contains or belongs to unavailable prototype content");
        missing_inventory_owners.insert(inventory.owner_id.value());
        ++report.dependent_record_count;
    }
    std::erase_if(snapshot.inventories, [&missing_inventory_owners](const auto& record) {
        return missing_inventory_owners.contains(record.owner_id.value());
    });

    std::set<std::uint64_t> missing_process_ids;
    for (const auto& process : snapshot.processes) {
        const auto owner_missing = missing_owner_ids.contains(process.owner_id.value());
        const core::PrototypeId* missing_slot_prototype = nullptr;
        const auto find_missing_slot = [&](const std::vector<processes::ProcessSlot>& slots) {
            return std::ranges::find_if(slots, [&prototypes](const auto& slot) {
                return prototype_missing(prototypes, slot.prototype_id);
            });
        };
        if (const auto input = find_missing_slot(process.input_slots);
            input != process.input_slots.end()) {
            missing_slot_prototype = &input->prototype_id;
        } else if (const auto output = find_missing_slot(process.output_slots);
                   output != process.output_slots.end()) {
            missing_slot_prototype = &output->prototype_id;
        }
        const auto process_prototype_missing = prototype_missing(prototypes, process.prototype_id);
        if (!owner_missing && !process_prototype_missing && missing_slot_prototype == nullptr) {
            continue;
        }
        const auto& unavailable_prototype = process_prototype_missing ? process.prototype_id
                                            : missing_slot_prototype != nullptr
                                                ? *missing_slot_prototype
                                                : process.prototype_id;
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::process,
            process.process_id.value(), unavailable_prototype, {}, process.owner_id,
            record_blob(snapshot,
                        [&process](SaveSnapshot& single) { single.processes.push_back(process); }),
            owner_missing               ? "process owner prototype is unavailable"
            : process_prototype_missing ? "process prototype is unavailable"
                                        : "process slot prototype is unavailable");
        missing_process_ids.insert(process.process_id.value());
        if (owner_missing)
            ++report.dependent_record_count;
    }
    std::erase_if(snapshot.processes, [&missing_process_ids](const auto& record) {
        return missing_process_ids.contains(record.process_id.value());
    });

    std::set<std::uint64_t> missing_fire_ids;
    for (const auto& fire : snapshot.fires) {
        const auto owner_missing = missing_owner_ids.contains(fire.fire_id.value());
        if (!owner_missing && !prototype_missing(prototypes, fire.prototype_id)) {
            continue;
        }
        const auto position = build_positions.contains(fire.fire_id.value())
                                  ? build_positions.at(fire.fire_id.value())
                                  : world::WorldPosition{};
        add_placeholder(
            snapshot, placeholder_keys, placeholder_error, world::MissingPrototypeKind::fire,
            fire.fire_id.value(), fire.prototype_id, position, fire.fire_id,
            record_blob(snapshot, [&fire](SaveSnapshot& single) { single.fires.push_back(fire); }),
            owner_missing ? "fire owner prototype is unavailable"
                          : "fire prototype is unavailable");
        missing_fire_ids.insert(fire.fire_id.value());
        if (owner_missing)
            ++report.dependent_record_count;
    }
    std::erase_if(snapshot.fires, [&missing_fire_ids](const auto& record) {
        return missing_fire_ids.contains(record.fire_id.value());
    });

    if (placeholder_error.has_value()) {
        return core::Result<MissingPrototypeRecoveryReport>::failure(placeholder_error->code,
                                                                     placeholder_error->message);
    }
    report.placeholder_count = snapshot.missing_prototypes.size();
    for (const auto& missing : snapshot.missing_prototypes) {
        auto status = missing.validate();
        if (!status) {
            return core::Result<MissingPrototypeRecoveryReport>::failure(status.error().code,
                                                                         status.error().message);
        }
    }
    return core::Result<MissingPrototypeRecoveryReport>::success(report);
}

core::Result<MissingPrototypeRecoveryReport>
preserve_missing_prototypes(SaveSnapshot& snapshot, const modding::PrototypeRegistry& prototypes) {
    auto staged = snapshot;
    auto recovered = preserve_missing_prototypes_in_place(staged, prototypes);
    if (!recovered) {
        return recovered;
    }
    snapshot = std::move(staged);
    return recovered;
}

} // namespace heartstead::save
