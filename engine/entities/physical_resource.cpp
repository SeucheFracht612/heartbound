#include "engine/entities/physical_resource.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace heartstead::entities {

namespace {

[[nodiscard]] bool state_requires_body(PhysicalResourceState state) noexcept {
    return state == PhysicalResourceState::dynamic ||
           state == PhysicalResourceState::settled_sleeping ||
           state == PhysicalResourceState::frozen_static;
}

[[nodiscard]] bool state_allows_body(PhysicalResourceState state) noexcept {
    return state == PhysicalResourceState::dynamic ||
           state == PhysicalResourceState::settled_sleeping ||
           state == PhysicalResourceState::frozen_static;
}

[[nodiscard]] physics::CompoundShapeChild
to_compound_child(const PhysicalResourceSegment& segment) noexcept {
    return physics::CompoundShapeChild{
        segment.shape,  segment.local_position, segment.half_extents,
        segment.radius, segment.half_height,
    };
}

[[nodiscard]] physics::PhysicsShapeDesc to_child_shape(const PhysicalResourceSegment& segment) {
    physics::PhysicsShapeDesc shape;
    shape.kind = segment.shape;
    shape.half_extents = segment.half_extents;
    shape.radius = segment.radius;
    shape.half_height = segment.half_height;
    return shape;
}

[[nodiscard]] bool can_convert_to_cargo(PhysicalResourceState state) noexcept {
    return state == PhysicalResourceState::settled_sleeping ||
           state == PhysicalResourceState::frozen_static;
}

} // namespace

core::Status PhysicalResourceRecord::validate() const {
    if (!resource_id.is_valid()) {
        return core::Status::failure("physical_resource.invalid_id",
                                     "physical resource needs a stable save id");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("physical_resource.invalid_prototype",
                                     "physical resource prototype id must be valid");
    }
    if (!cargo_prototype_id.is_valid()) {
        return core::Status::failure("physical_resource.invalid_cargo_prototype",
                                     "physical resource cargo prototype id must be valid");
    }
    if (mass_grams == 0) {
        return core::Status::failure("physical_resource.invalid_mass",
                                     "physical resource mass must be non-zero");
    }
    if (volume_milliliters == 0) {
        return core::Status::failure("physical_resource.invalid_volume",
                                     "physical resource volume must be non-zero");
    }
    if (stability_per_mille < 0 || stability_per_mille > 1000) {
        return core::Status::failure("physical_resource.invalid_stability",
                                     "physical resource stability must be between 0 and 1000");
    }
    if (allowed_transport_modes.empty()) {
        return core::Status::failure("physical_resource.no_transport_modes",
                                     "physical resource must declare cargo transport modes");
    }
    if (segments.empty()) {
        return core::Status::failure("physical_resource.no_segments",
                                     "physical resource needs at least one compound segment");
    }
    if (state_requires_body(state) && !physics_body_id.is_valid()) {
        return core::Status::failure("physical_resource.missing_physics_body",
                                     "active physical resource state requires a physics body id");
    }
    if (!state_allows_body(state) && physics_body_id.is_valid()) {
        return core::Status::failure(
            "physical_resource.unexpected_physics_body",
            "inactive physical resource state must not keep a physics body id");
    }
    for (const auto& tag : hazard_tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure("physical_resource.invalid_hazard_tag",
                                         "physical resource hazard tag is invalid: " + tag);
        }
    }
    for (const auto& segment : segments) {
        if (!segment.local_position.is_finite()) {
            return core::Status::failure("physical_resource.invalid_segment",
                                         "physical resource segment position must be finite");
        }
        auto shape_status = physics::validate_physics_shape_desc(to_child_shape(segment));
        if (!shape_status) {
            return core::Status::failure(shape_status.error().code, shape_status.error().message);
        }
    }
    return core::Status::ok();
}

core::Result<physics::PhysicsBodyDesc>
make_physical_resource_body_desc(const PhysicalResourceRecord& resource, physics::Vec3 position,
                                 physics::Vec3 linear_velocity) {
    if (!position.is_finite() || !linear_velocity.is_finite()) {
        return core::Result<physics::PhysicsBodyDesc>::failure(
            "physical_resource.invalid_body_transform",
            "physical resource body position and velocity must be finite");
    }
    if (resource.state == PhysicalResourceState::converted_to_cargo) {
        return core::Result<physics::PhysicsBodyDesc>::failure(
            "physical_resource.already_cargo",
            "converted physical resources cannot create physics bodies");
    }
    auto status = resource.validate();
    if (!status && status.error().code != "physical_resource.missing_physics_body") {
        return core::Result<physics::PhysicsBodyDesc>::failure(status.error().code,
                                                               status.error().message);
    }
    if (resource.segments.empty()) {
        return core::Result<physics::PhysicsBodyDesc>::failure(
            "physical_resource.no_segments",
            "physical resource needs segments before creating a physics body");
    }

    physics::PhysicsBodyDesc desc;
    desc.motion_type = physics::BodyMotionType::dynamic;
    desc.mass = static_cast<float>(resource.mass_grams) / 1000.0F;
    desc.position = position;
    desc.linear_velocity = linear_velocity;
    desc.user_data = resource.resource_id.value();
    desc.shape.kind = physics::ShapeKind::compound;
    desc.shape.children.reserve(resource.segments.size());
    for (const auto& segment : resource.segments) {
        desc.shape.children.push_back(to_compound_child(segment));
    }

    status = physics::validate_physics_body_desc(desc);
    if (!status) {
        return core::Result<physics::PhysicsBodyDesc>::failure(status.error().code,
                                                               status.error().message);
    }
    return core::Result<physics::PhysicsBodyDesc>::success(std::move(desc));
}

core::Status attach_physical_resource_body(PhysicalResourceRecord& resource,
                                           physics::PhysicsBodyId body_id) {
    if (!body_id.is_valid()) {
        return core::Status::failure("physical_resource.invalid_physics_body",
                                     "attached physics body id must be valid");
    }
    if (resource.state == PhysicalResourceState::converted_to_cargo) {
        return core::Status::failure("physical_resource.already_cargo",
                                     "converted physical resources cannot attach physics bodies");
    }
    resource.physics_body_id = body_id;
    resource.state = PhysicalResourceState::dynamic;
    return resource.validate();
}

core::Status mark_physical_resource_settled(PhysicalResourceRecord& resource) {
    if (resource.state != PhysicalResourceState::dynamic) {
        return core::Status::failure("physical_resource.not_dynamic",
                                     "only dynamic physical resources can become settled");
    }
    resource.state = PhysicalResourceState::settled_sleeping;
    return resource.validate();
}

core::Status freeze_physical_resource(PhysicalResourceRecord& resource) {
    if (resource.state != PhysicalResourceState::settled_sleeping) {
        return core::Status::failure("physical_resource.not_settled",
                                     "only settled physical resources can freeze static");
    }
    resource.state = PhysicalResourceState::frozen_static;
    return resource.validate();
}

core::Result<cargo::CargoRecord>
convert_physical_resource_to_cargo(PhysicalResourceRecord& resource, core::SaveId cargo_id) {
    if (!can_convert_to_cargo(resource.state)) {
        return core::Result<cargo::CargoRecord>::failure(
            "physical_resource.not_convertible",
            "physical resource must be settled or frozen before cargo conversion");
    }
    auto status = resource.validate();
    if (!status) {
        return core::Result<cargo::CargoRecord>::failure(status.error().code,
                                                         status.error().message);
    }

    cargo::CargoRecord cargo_record;
    cargo_record.cargo_id = cargo_id;
    cargo_record.prototype_id = resource.cargo_prototype_id;
    cargo_record.mass_grams = resource.mass_grams;
    cargo_record.volume_milliliters = resource.volume_milliliters;
    cargo_record.stability_per_mille = resource.stability_per_mille;
    cargo_record.allowed_transport_modes = resource.allowed_transport_modes;
    cargo_record.hazard_tags = resource.hazard_tags;
    status = cargo_record.validate();
    if (!status) {
        return core::Result<cargo::CargoRecord>::failure(status.error().code,
                                                         status.error().message);
    }

    resource.state = PhysicalResourceState::converted_to_cargo;
    resource.physics_body_id = {};
    return core::Result<cargo::CargoRecord>::success(std::move(cargo_record));
}

std::string_view physical_resource_kind_name(PhysicalResourceKind kind) noexcept {
    switch (kind) {
    case PhysicalResourceKind::felled_tree:
        return "felled_tree";
    case PhysicalResourceKind::haulable_log:
        return "haulable_log";
    case PhysicalResourceKind::stone_block:
        return "stone_block";
    }
    return "unknown";
}

std::string_view physical_resource_state_name(PhysicalResourceState state) noexcept {
    switch (state) {
    case PhysicalResourceState::cutting:
        return "cutting";
    case PhysicalResourceState::dynamic:
        return "dynamic";
    case PhysicalResourceState::settled_sleeping:
        return "settled_sleeping";
    case PhysicalResourceState::frozen_static:
        return "frozen_static";
    case PhysicalResourceState::converted_to_cargo:
        return "converted_to_cargo";
    }
    return "unknown";
}

} // namespace heartstead::entities
