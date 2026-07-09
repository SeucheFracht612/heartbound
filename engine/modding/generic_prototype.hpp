#pragma once

#include "engine/core/ids.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace heartstead::modding {

enum class GenericPrototypePatchStage {
    data_update,
    final_fix,
};

struct GenericPrototype {
    std::string kind;
    core::PrototypeId id;
    std::string display_name;
    std::filesystem::path source;
    std::unordered_map<std::string, std::string> fields;
};

struct GenericPrototypePatch {
    std::string source_mod_id;
    GenericPrototypePatchStage stage = GenericPrototypePatchStage::data_update;
    core::PrototypeId target_id;
    std::filesystem::path source;
    std::unordered_map<std::string, std::string> set_fields;
};

[[nodiscard]] std::string_view
generic_prototype_patch_stage_name(GenericPrototypePatchStage stage) noexcept;

} // namespace heartstead::modding
