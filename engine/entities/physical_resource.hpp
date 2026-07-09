#pragma once

#include "engine/cargo/cargo.hpp"
#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/physics/physics_world.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::entities {

enum class PhysicalResourceKind {
    felled_tree,
    haulable_log,
    stone_block,
};

enum class PhysicalResourceState {
    cutting,
    dynamic,
    settled_sleeping,
    frozen_static,
    converted_to_cargo,
};

struct PhysicalResourceSegment {
    physics::ShapeKind shape = physics::ShapeKind::box;
    physics::Vec3 local_position{};
    physics::Vec3 half_extents{0.5F, 0.5F, 0.5F};
    float radius = 0.5F;
    float half_height = 0.5F;
};

struct PhysicalResourceRecord {
    core::SaveId resource_id;
    core::PrototypeId prototype_id;
    core::PrototypeId cargo_prototype_id;
    PhysicalResourceKind kind = PhysicalResourceKind::felled_tree;
    PhysicalResourceState state = PhysicalResourceState::cutting;
    physics::PhysicsBodyId physics_body_id;
    std::uint64_t mass_grams = 0;
    std::uint64_t volume_milliliters = 0;
    std::int32_t stability_per_mille = 1000;
    cargo::CargoTransportModes allowed_transport_modes;
    std::vector<std::string> hazard_tags;
    std::vector<PhysicalResourceSegment> segments;

    [[nodiscard]] core::Status validate() const;
};

[[nodiscard]] core::Result<physics::PhysicsBodyDesc>
make_physical_resource_body_desc(const PhysicalResourceRecord& resource,
                                 physics::Vec3 position = {}, physics::Vec3 linear_velocity = {});

[[nodiscard]] core::Status attach_physical_resource_body(PhysicalResourceRecord& resource,
                                                         physics::PhysicsBodyId body_id);
[[nodiscard]] core::Status mark_physical_resource_settled(PhysicalResourceRecord& resource);
[[nodiscard]] core::Status freeze_physical_resource(PhysicalResourceRecord& resource);
[[nodiscard]] core::Result<cargo::CargoRecord>
convert_physical_resource_to_cargo(PhysicalResourceRecord& resource, core::SaveId cargo_id);

[[nodiscard]] std::string_view physical_resource_kind_name(PhysicalResourceKind kind) noexcept;
[[nodiscard]] std::string_view physical_resource_state_name(PhysicalResourceState state) noexcept;

} // namespace heartstead::entities
