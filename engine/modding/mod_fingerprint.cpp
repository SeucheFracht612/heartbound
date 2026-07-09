#include "engine/modding/mod_fingerprint.hpp"

#include "engine/core/hash.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::modding {

namespace {

void add_field(core::StableHash64& hasher, std::string_view key, std::string_view value) noexcept {
    hasher.add_string(key);
    hasher.add_string("\x1F");
    hasher.add_string(value);
    hasher.add_string("\x1E");
}

[[nodiscard]] std::vector<const GenericPrototype*>
prototypes_for_mod(const ModManifest& mod, const std::vector<GenericPrototype>& prototypes) {
    std::vector<const GenericPrototype*> owned;
    for (const auto& prototype : prototypes) {
        if (prototype.id.is_valid() && prototype.id.namespace_id() == mod.id) {
            owned.push_back(&prototype);
        }
    }

    std::ranges::sort(owned, {},
                      [](const GenericPrototype* prototype) { return prototype->id.value(); });
    return owned;
}

[[nodiscard]] std::string patch_field_sort_key(const GenericPrototypePatch& patch) {
    std::string output;
    std::map<std::string, std::string> sorted_fields(patch.set_fields.begin(),
                                                     patch.set_fields.end());
    for (const auto& [key, value] : sorted_fields) {
        output += key;
        output += '\x1F';
        output += value;
        output += '\x1E';
    }
    return output;
}

[[nodiscard]] std::vector<const GenericPrototypePatch*>
patches_for_mod(const ModManifest& mod,
                const std::vector<GenericPrototypePatch>& prototype_patches) {
    std::vector<const GenericPrototypePatch*> owned;
    for (const auto& patch : prototype_patches) {
        if (patch.source_mod_id == mod.id) {
            owned.push_back(&patch);
        }
    }

    std::ranges::sort(owned,
                      [](const GenericPrototypePatch* left, const GenericPrototypePatch* right) {
                          if (left->target_id.value() != right->target_id.value()) {
                              return left->target_id.value() < right->target_id.value();
                          }
                          const auto left_stage = generic_prototype_patch_stage_name(left->stage);
                          const auto right_stage = generic_prototype_patch_stage_name(right->stage);
                          if (left_stage != right_stage) {
                              return left_stage < right_stage;
                          }
                          if (left->source.generic_string() != right->source.generic_string()) {
                              return left->source.generic_string() < right->source.generic_string();
                          }
                          return patch_field_sort_key(*left) < patch_field_sort_key(*right);
                      });
    return owned;
}

[[nodiscard]] std::string
fingerprint_mod(const ModManifest& mod, const std::vector<const GenericPrototype*>& prototypes,
                const std::vector<const GenericPrototypePatch*>& patches) {
    core::StableHash64 hasher;
    add_field(hasher, "mod", mod.id);
    add_field(hasher, "prototype_count", std::to_string(prototypes.size()));
    add_field(hasher, "patch_count", std::to_string(patches.size()));

    for (const auto* prototype : prototypes) {
        add_field(hasher, "prototype", prototype->id.value());
        add_field(hasher, "kind", prototype->kind);
        add_field(hasher, "display_name", prototype->display_name);

        std::map<std::string, std::string> sorted_fields(prototype->fields.begin(),
                                                         prototype->fields.end());
        for (const auto& [key, value] : sorted_fields) {
            add_field(hasher, key, value);
        }
    }

    for (const auto* patch : patches) {
        add_field(hasher, "patch_target", patch->target_id.value());
        add_field(hasher, "patch_stage", generic_prototype_patch_stage_name(patch->stage));

        std::map<std::string, std::string> sorted_fields(patch->set_fields.begin(),
                                                         patch->set_fields.end());
        for (const auto& [key, value] : sorted_fields) {
            add_field(hasher, "patch.set." + key, value);
        }
    }

    return hasher.hex();
}

} // namespace

std::vector<ModPrototypeFingerprint>
build_mod_prototype_fingerprints(const std::vector<ModManifest>& mods,
                                 const std::vector<GenericPrototype>& prototypes) {
    return build_mod_prototype_fingerprints(mods, prototypes, {});
}

std::vector<ModPrototypeFingerprint>
build_mod_prototype_fingerprints(const std::vector<ModManifest>& mods,
                                 const std::vector<GenericPrototype>& prototypes,
                                 const std::vector<GenericPrototypePatch>& prototype_patches) {
    std::vector<ModPrototypeFingerprint> fingerprints;
    fingerprints.reserve(mods.size());

    for (const auto& mod : mods) {
        auto owned_prototypes = prototypes_for_mod(mod, prototypes);
        auto owned_patches = patches_for_mod(mod, prototype_patches);
        fingerprints.push_back(ModPrototypeFingerprint{
            mod.id,
            mod.version,
            fingerprint_mod(mod, owned_prototypes, owned_patches),
            owned_prototypes.size(),
            owned_patches.size(),
        });
    }

    std::ranges::sort(fingerprints, {}, &ModPrototypeFingerprint::id);
    return fingerprints;
}

} // namespace heartstead::modding
