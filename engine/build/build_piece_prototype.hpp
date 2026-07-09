#pragma once

#include "engine/build/build_piece.hpp"
#include "engine/modding/generic_prototype.hpp"

#include <string_view>

namespace heartstead::build {

[[nodiscard]] networks::NetworkKind
network_kind_for_build_port(std::string_view port_name) noexcept;

[[nodiscard]] core::Result<BuildPieceRecord>
build_piece_record_from_prototype(const modding::GenericPrototype& prototype,
                                  core::SaveId object_id, Transform transform = {});

} // namespace heartstead::build
