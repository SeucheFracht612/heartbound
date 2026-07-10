#include "engine/world/missing_prototype.hpp"

namespace heartstead::world {

core::Status MissingPrototypeObject::validate() const {
    if (stable_id == 0) {
        return core::Status::failure("missing_prototype.invalid_id",
                                     "missing prototype placeholder needs a stable id");
    }
    if (!original_prototype_id.is_valid()) {
        return core::Status::failure(
            "missing_prototype.invalid_prototype",
            "missing prototype placeholder needs the original prototype id");
    }
    if (!position.is_valid()) {
        return core::Status::failure("missing_prototype.invalid_position",
                                     "missing prototype placeholder position is invalid");
    }
    if (saved_blob.empty()) {
        return core::Status::failure("missing_prototype.empty_blob",
                                     "missing prototype placeholder must retain its saved blob");
    }
    return core::Status::ok();
}

std::string_view missing_prototype_kind_name(MissingPrototypeKind kind) noexcept {
    switch (kind) {
    case MissingPrototypeKind::build_piece:
        return "build_piece";
    case MissingPrototypeKind::entity:
        return "entity";
    case MissingPrototypeKind::cargo:
        return "cargo";
    case MissingPrototypeKind::inventory:
        return "inventory";
    case MissingPrototypeKind::workpiece:
        return "workpiece";
    case MissingPrototypeKind::assembly:
        return "assembly";
    case MissingPrototypeKind::process:
        return "process";
    case MissingPrototypeKind::fire:
        return "fire";
    }
    return "unknown";
}

std::optional<MissingPrototypeKind>
missing_prototype_kind_from_name(std::string_view value) noexcept {
    if (value == "build_piece")
        return MissingPrototypeKind::build_piece;
    if (value == "entity")
        return MissingPrototypeKind::entity;
    if (value == "cargo")
        return MissingPrototypeKind::cargo;
    if (value == "inventory")
        return MissingPrototypeKind::inventory;
    if (value == "workpiece")
        return MissingPrototypeKind::workpiece;
    if (value == "assembly")
        return MissingPrototypeKind::assembly;
    if (value == "process")
        return MissingPrototypeKind::process;
    if (value == "fire")
        return MissingPrototypeKind::fire;
    return std::nullopt;
}

} // namespace heartstead::world
