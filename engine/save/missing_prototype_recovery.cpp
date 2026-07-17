#include "engine/save/missing_prototype_recovery.hpp"

#include "engine/save/save_text_codec.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace heartstead::save {

namespace {

[[nodiscard]] bool missing_kind(const modding::PrototypeRegistry& prototypes,
                                const core::PrototypeId& id, std::string_view kind) {
    const auto* prototype = prototypes.find(id);
    return prototype == nullptr || prototype->kind != kind;
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
                     world::MissingPrototypeKind kind, std::uint64_t stable_id,
                     const core::PrototypeId& prototype_id, world::WorldPosition position,
                     core::SaveId owner_id, std::string blob, std::string warning) {
    if (!placeholder_keys.emplace(kind, stable_id).second) {
        return;
    }
    snapshot.missing_prototypes.push_back(
        {kind, stable_id, prototype_id, position, owner_id, std::move(blob), std::move(warning)});
}

} // namespace

core::Result<MissingPrototypeRecoveryReport>
preserve_missing_prototypes(SaveSnapshot& snapshot, const modding::PrototypeRegistry& prototypes) {
    MissingPrototypeRecoveryReport report;
    std::set<MissingPlaceholderKey> placeholder_keys;
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
        if (!missing_kind(prototypes, build.prototype_id, modding::PrototypeKinds::build_piece)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, world::MissingPrototypeKind::build_piece,
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
        if (!missing_kind(prototypes, entity.prototype_id, modding::PrototypeKinds::entity)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, world::MissingPrototypeKind::entity, entity.save_id.value(),
            entity.prototype_id, entity.transform.position, {},
            record_blob(snapshot,
                        [&entity](SaveSnapshot& single) { single.entities.push_back(entity); }),
            "entity prototype is unavailable");
        missing_entity_ids.insert(entity.save_id.value());
        missing_owner_ids.insert(entity.save_id.value());
        missing_owner_prototypes.emplace(entity.save_id.value(), entity.prototype_id);
    }
    std::erase_if(snapshot.entities, [&missing_entity_ids](const auto& record) {
        return missing_entity_ids.contains(record.save_id.value());
    });

    std::set<std::uint64_t> missing_cargo_ids;
    for (const auto& cargo : snapshot.cargo_records) {
        if (!missing_kind(prototypes, cargo.prototype_id, modding::PrototypeKinds::cargo)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, world::MissingPrototypeKind::cargo, cargo.cargo_id.value(),
            cargo.prototype_id, cargo.position, {},
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
        if (!missing_kind(prototypes, workpiece.prototype_id, modding::PrototypeKinds::workpiece)) {
            continue;
        }
        add_placeholder(snapshot, placeholder_keys, world::MissingPrototypeKind::workpiece,
                        workpiece.workpiece_id.value(), workpiece.prototype_id, {}, {},
                        record_blob(snapshot,
                                    [&workpiece](SaveSnapshot& single) {
                                        single.workpieces.push_back(workpiece);
                                    }),
                        "workpiece prototype is unavailable");
        missing_workpiece_ids.insert(workpiece.workpiece_id.value());
    }
    std::erase_if(snapshot.workpieces, [&missing_workpiece_ids](const auto& record) {
        return missing_workpiece_ids.contains(record.workpiece_id.value());
    });

    std::set<std::uint64_t> missing_assembly_ids;
    for (const auto& assembly : snapshot.assemblies) {
        bool dependent_missing = missing_owner_ids.contains(assembly.root_build_piece_id.value());
        for (const auto& part : assembly.parts) {
            dependent_missing =
                dependent_missing || missing_owner_ids.contains(part.build_piece_id.value());
        }
        if (!dependent_missing &&
            !missing_kind(prototypes, assembly.prototype_id, modding::PrototypeKinds::assembly)) {
            continue;
        }
        const auto position = build_positions.contains(assembly.root_build_piece_id.value())
                                  ? build_positions.at(assembly.root_build_piece_id.value())
                                  : world::WorldPosition{};
        add_placeholder(snapshot, placeholder_keys, world::MissingPrototypeKind::assembly,
                        assembly.assembly_id.value(), assembly.prototype_id, position,
                        assembly.root_build_piece_id,
                        record_blob(snapshot,
                                    [&assembly](SaveSnapshot& single) {
                                        single.assemblies.push_back(assembly);
                                    }),
                        dependent_missing ? "assembly depends on an unavailable part prototype"
                                          : "assembly prototype is unavailable");
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
            if (missing_kind(prototypes, stack.prototype_id, modding::PrototypeKinds::item)) {
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
        add_placeholder(snapshot, placeholder_keys, world::MissingPrototypeKind::inventory,
                        inventory.owner_id.value(), prototype, {}, inventory.owner_id,
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
        if (!owner_missing &&
            !missing_kind(prototypes, process.prototype_id, modding::PrototypeKinds::process)) {
            continue;
        }
        add_placeholder(
            snapshot, placeholder_keys, world::MissingPrototypeKind::process,
            process.process_id.value(), process.prototype_id, {}, process.owner_id,
            record_blob(snapshot,
                        [&process](SaveSnapshot& single) { single.processes.push_back(process); }),
            owner_missing ? "process owner prototype is unavailable"
                          : "process prototype is unavailable");
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
        if (!owner_missing &&
            !missing_kind(prototypes, fire.prototype_id, modding::PrototypeKinds::fire)) {
            continue;
        }
        const auto position = build_positions.contains(fire.fire_id.value())
                                  ? build_positions.at(fire.fire_id.value())
                                  : world::WorldPosition{};
        add_placeholder(
            snapshot, placeholder_keys, world::MissingPrototypeKind::fire, fire.fire_id.value(),
            fire.prototype_id, position, fire.fire_id,
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

} // namespace heartstead::save
