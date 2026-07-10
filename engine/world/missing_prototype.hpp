#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/world/coords/world_position.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace heartstead::world {

enum class MissingPrototypeKind {
    build_piece,
    entity,
    cargo,
    inventory,
    workpiece,
    assembly,
    process,
    fire,
};

struct MissingPrototypeObject {
    MissingPrototypeKind kind = MissingPrototypeKind::build_piece;
    std::uint64_t stable_id = 0;
    core::PrototypeId original_prototype_id;
    WorldPosition position;
    core::SaveId owner_id;
    std::string saved_blob;
    std::string warning;

    [[nodiscard]] core::Status validate() const;
};

[[nodiscard]] std::string_view missing_prototype_kind_name(MissingPrototypeKind kind) noexcept;
[[nodiscard]] std::optional<MissingPrototypeKind>
missing_prototype_kind_from_name(std::string_view value) noexcept;

} // namespace heartstead::world
