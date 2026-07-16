#pragma once

#include <compare>
#include <cstdint>

namespace heartstead::renderer {

template <typename Tag> struct RenderAssetHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index != 0 && generation != 0;
    }

    friend constexpr auto operator<=>(const RenderAssetHandle&, const RenderAssetHandle&) = default;
};

struct ShaderProgramTag;
struct TextureTag;
struct MaterialRuntimeTag;
struct RenderMeshTag;

using ShaderProgramHandle = RenderAssetHandle<ShaderProgramTag>;
using TextureHandle = RenderAssetHandle<TextureTag>;
using MaterialRuntimeHandle = RenderAssetHandle<MaterialRuntimeTag>;
using RenderMeshHandle = RenderAssetHandle<RenderMeshTag>;

} // namespace heartstead::renderer
