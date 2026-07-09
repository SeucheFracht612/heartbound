#pragma once

#include "engine/assets/asset_catalog.hpp"
#include "engine/core/result.hpp"
#include "engine/modding/mod_diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer::shaders {

enum class ShaderSourceLanguage {
    slang,
    hlsl,
    spirv,
    unknown,
};

enum class ShaderSourceRole {
    template_source,
    vertex,
    fragment,
    compute,
    library,
    unknown,
};

struct CompiledShaderRecord {
    std::string logical_id;
    assets::VirtualPath source_virtual_path;
    assets::AssetSourceKind source_kind = assets::AssetSourceKind::mod;
    std::string source_id;
    std::string source_hash;
    ShaderSourceLanguage language = ShaderSourceLanguage::unknown;
    ShaderSourceRole role = ShaderSourceRole::unknown;
    std::filesystem::path compiled_relative_path;
    std::string compiled_hash;
    std::string backend;
    std::uint32_t pipeline_version = 1;
    std::size_t source_bytes = 0;
};

struct ShaderCompileConfig {
    std::filesystem::path output_root;
    std::filesystem::path manifest_relative_path = "shader_manifest.txt";
    std::string profile = "development";
    std::uint32_t pipeline_version = 1;
};

struct ShaderCompileResult {
    std::vector<CompiledShaderRecord> records;
    std::vector<modding::ModDiagnostic> diagnostics;
    std::filesystem::path manifest_path;
    std::size_t compiled_shader_count = 0;
    std::uintmax_t compiled_payload_bytes = 0;

    [[nodiscard]] bool has_errors() const noexcept;
};

class ShaderCompiler {
  public:
    [[nodiscard]] static core::Result<ShaderCompileResult>
    compile(const assets::AssetCatalog& catalog, ShaderCompileConfig config);
};

[[nodiscard]] ShaderSourceLanguage infer_shader_language(const std::filesystem::path& path);
[[nodiscard]] ShaderSourceRole infer_shader_role(const std::filesystem::path& path);
[[nodiscard]] core::Status validate_spirv_shader_bytes(std::span<const std::uint8_t> bytes,
                                                       std::string_view source_name);
[[nodiscard]] std::string_view shader_source_language_name(ShaderSourceLanguage language) noexcept;
[[nodiscard]] std::string_view shader_source_role_name(ShaderSourceRole role) noexcept;
[[nodiscard]] std::string_view shader_compile_backend_name(ShaderSourceLanguage language) noexcept;
[[nodiscard]] std::string_view shader_compile_backend_name(ShaderSourceLanguage language,
                                                           std::string_view profile) noexcept;

} // namespace heartstead::renderer::shaders
