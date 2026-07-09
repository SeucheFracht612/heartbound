#pragma once

#include "engine/core/result.hpp"
#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/mod_diagnostic.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::modding {

struct PrototypeKinds {
    static constexpr std::string_view item = "item";
    static constexpr std::string_view cargo = "cargo";
    static constexpr std::string_view entity = "entity";
    static constexpr std::string_view voxel = "voxel";
    static constexpr std::string_view build_piece = "build_piece";
    static constexpr std::string_view assembly = "assembly";
    static constexpr std::string_view workpiece = "workpiece";
    static constexpr std::string_view process = "process";
    static constexpr std::string_view room_descriptor = "room_descriptor";
    static constexpr std::string_view material = "material";
    static constexpr std::string_view scenario = "scenario";
};

struct PrototypeRegistryBuildResult {
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class PrototypeRegistry {
  public:
    [[nodiscard]] PrototypeRegistryBuildResult build(std::vector<GenericPrototype> prototypes);

    [[nodiscard]] const GenericPrototype* find(const core::PrototypeId& id) const noexcept;
    [[nodiscard]] bool contains(const core::PrototypeId& id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t count_kind(std::string_view kind) const noexcept;
    [[nodiscard]] std::vector<const GenericPrototype*>
    prototypes_of_kind(std::string_view kind) const;

    [[nodiscard]] core::Status require(const core::PrototypeId& id) const;
    [[nodiscard]] core::Status require_kind(const core::PrototypeId& id,
                                            std::string_view expected_kind) const;

  private:
    std::vector<GenericPrototype> prototypes_;
    std::unordered_map<std::string, std::size_t> by_id_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_kind_;
};

} // namespace heartstead::modding
