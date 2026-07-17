#include "engine/renderer/shaders/shader_compiler.hpp"

#include "engine/core/file_io.hpp"
#include "engine/core/hash.hpp"
#include "engine/core/ids.hpp"
#include "engine/renderer/shaders/spirv_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::renderer::shaders {

namespace {

constexpr std::string_view manifest_magic = "heartstead.shader_manifest.v1";

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool is_production_profile(std::string_view profile) noexcept {
    return profile == "production";
}

[[nodiscard]] bool has_path_segment(const std::filesystem::path& path, std::string_view segment) {
    return std::ranges::any_of(path, [segment](const std::filesystem::path& part) {
        return part.generic_string() == segment;
    });
}

[[nodiscard]] bool filename_contains(const std::filesystem::path& path, std::string_view needle) {
    return lower_ascii(path.filename().generic_string()).find(needle) != std::string::npos;
}

[[nodiscard]] bool is_valid_relative_path(const std::filesystem::path& path) {
    return assets::is_safe_asset_relative_path(path) &&
           core::is_valid_local_id(path.generic_string());
}

[[nodiscard]] std::filesystem::path make_compiled_path(const assets::AssetRecord& source,
                                                       const ShaderCompileConfig& config,
                                                       ShaderSourceRole role) {
    std::filesystem::path path;
    path /= source.source_id;
    path /= config.profile;
    path /= std::string(shader_source_role_name(role));
    const auto logical_path = assets::asset_logical_path(source.logical_id);
    if (logical_path)
        path /= logical_path.value();
    path += ".shadercooked";
    return path;
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>>
read_file_bytes(const std::filesystem::path& path, std::size_t maximum_bytes) {
    auto bytes = core::read_binary_file(path, {.maximum_bytes = maximum_bytes});
    if (!bytes) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            bytes.error().code == "core.file_too_large" ? "shader_compiler.source_too_large"
                                                        : "shader_compiler.read_failed",
            bytes.error().message);
    }
    return bytes;
}

void add_diagnostic(ShaderCompileResult& result, modding::DiagnosticSeverity severity,
                    std::filesystem::path source, std::string code, std::string message) {
    result.diagnostics.push_back(modding::ModDiagnostic{
        severity,
        std::move(source),
        std::move(code),
        std::move(message),
    });
}

[[nodiscard]] core::Status validate_spirv_bytes(std::span<const std::uint8_t> bytes,
                                                std::string_view source_name) {
    auto status = validate_spirv(bytes);
    if (!status) {
        return core::Status::failure("shader_compiler.invalid_spirv",
                                     "production SPIR-V shader is invalid: " +
                                         std::string(source_name) + ": " + status.error().message);
    }
    return core::Status::ok();
}

[[nodiscard]] std::string percent_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : input) {
        const auto byte = static_cast<unsigned char>(value);
        if (value == '%' || value == '|' || value == '=' || value == '\n' || value == '\r') {
            result.push_back('%');
            result.push_back(hex[(byte >> 4u) & 0x0Fu]);
            result.push_back(hex[byte & 0x0Fu]);
        } else {
            result.push_back(value);
        }
    }

    return result;
}

[[nodiscard]] std::string encode_record(const CompiledShaderRecord& record) {
    std::ostringstream output;
    output << percent_escape(record.logical_id) << '|'
           << percent_escape(record.source_virtual_path.to_string()) << '|'
           << assets::asset_source_kind_name(record.source_kind) << '|'
           << percent_escape(record.source_id) << '|' << percent_escape(record.source_hash) << '|'
           << shader_source_language_name(record.language) << '|'
           << shader_source_role_name(record.role) << '|'
           << percent_escape(record.compiled_relative_path.generic_string()) << '|'
           << percent_escape(record.compiled_hash) << '|' << percent_escape(record.backend) << '|'
           << record.pipeline_version << '|' << record.source_bytes;
    return output.str();
}

[[nodiscard]] std::string encode_manifest(const ShaderCompileConfig& config,
                                          const std::vector<CompiledShaderRecord>& records) {
    std::ostringstream output;
    output << manifest_magic << '\n';
    output << "profile=" << percent_escape(config.profile) << '\n';
    output << "pipeline_version=" << config.pipeline_version << '\n';
    for (const auto& record : records) {
        output << "shader=" << encode_record(record) << '\n';
    }
    output << "end\n";
    return output.str();
}

[[nodiscard]] core::Status write_compiled_payload(const std::filesystem::path& path,
                                                  const CompiledShaderRecord& record,
                                                  std::span<const std::uint8_t> source_bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return core::Status::failure("shader_compiler.create_directory_failed", error.message());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return core::Status::failure("shader_compiler.write_failed",
                                     "failed to open compiled shader output: " + path.string());
    }

    output << "heartstead.compiled_shader.v1\n";
    output << "backend=" << record.backend << '\n';
    output << "logical_id=" << record.logical_id << '\n';
    output << "language=" << shader_source_language_name(record.language) << '\n';
    output << "role=" << shader_source_role_name(record.role) << '\n';
    output << "source_virtual_path=" << record.source_virtual_path.to_string() << '\n';
    output << "source_hash=" << record.source_hash << '\n';
    output << "pipeline_version=" << record.pipeline_version << '\n';
    output << "source_bytes=" << record.source_bytes << '\n';
    output << "---\n";
    if (!source_bytes.empty()) {
        output.write(reinterpret_cast<const char*>(source_bytes.data()),
                     static_cast<std::streamsize>(source_bytes.size()));
    }
    if (!output) {
        return core::Status::failure("shader_compiler.write_failed",
                                     "failed to write compiled shader output: " + path.string());
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status write_manifest_file(const std::filesystem::path& path,
                                               std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return core::Status::failure("shader_compiler.create_directory_failed", error.message());
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return core::Status::failure("shader_compiler.write_failed",
                                     "failed to open shader manifest: " + path.string());
    }
    output << text;
    if (!output) {
        return core::Status::failure("shader_compiler.write_failed",
                                     "failed to write shader manifest: " + path.string());
    }
    return core::Status::ok();
}

} // namespace

bool ShaderCompileResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const modding::ModDiagnostic& diagnostic) {
        return diagnostic.severity == modding::DiagnosticSeverity::error;
    });
}

core::Status validate_spirv_shader_bytes(std::span<const std::uint8_t> bytes,
                                         std::string_view source_name) {
    return validate_spirv_bytes(bytes, source_name);
}

core::Result<ShaderCompileResult> ShaderCompiler::compile(const assets::AssetCatalog& catalog,
                                                          ShaderCompileConfig config) {
    if (config.output_root.empty()) {
        return core::Result<ShaderCompileResult>::failure(
            "shader_compiler.invalid_output_root", "shader compiler output root is required");
    }
    if (!assets::is_safe_asset_relative_path(config.manifest_relative_path)) {
        return core::Result<ShaderCompileResult>::failure(
            "shader_compiler.invalid_manifest_path",
            "shader compiler manifest path must be a safe relative path");
    }
    if (!core::is_valid_namespace_id(config.profile)) {
        return core::Result<ShaderCompileResult>::failure(
            "shader_compiler.invalid_profile", "shader compiler profile must be a safe id");
    }
    if (config.pipeline_version == 0) {
        return core::Result<ShaderCompileResult>::failure(
            "shader_compiler.invalid_pipeline_version",
            "shader compiler pipeline version must be non-zero");
    }
    if (config.maximum_source_bytes == 0) {
        return core::Result<ShaderCompileResult>::failure(
            "shader_compiler.invalid_source_limit",
            "shader compiler source byte limit must be non-zero");
    }

    auto output_root = assets::canonical_asset_root(config.output_root);
    if (!output_root) {
        return core::Result<ShaderCompileResult>::failure(output_root.error().code,
                                                          output_root.error().message);
    }
    auto manifest_path =
        assets::resolve_asset_path(output_root.value(), config.manifest_relative_path);
    if (!manifest_path) {
        return core::Result<ShaderCompileResult>::failure(manifest_path.error().code,
                                                          manifest_path.error().message);
    }

    ShaderCompileResult result;
    result.manifest_path = std::move(manifest_path).value();
    struct PendingShader {
        CompiledShaderRecord record;
        std::vector<std::uint8_t> source_bytes;
    };
    std::vector<PendingShader> pending_shaders;

    for (const auto* source : catalog.active_records()) {
        if (source->kind != assets::AssetKind::shader) {
            continue;
        }

        const auto language = infer_shader_language(source->virtual_path.relative_path);
        const auto role = infer_shader_role(source->virtual_path.relative_path);
        if (language == ShaderSourceLanguage::unknown) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           "shader_compiler.unknown_language",
                           "shader source has an unsupported language: " +
                               source->virtual_path.to_string());
            continue;
        }
        if (role == ShaderSourceRole::unknown) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           "shader_compiler.unknown_role",
                           "shader source role could not be inferred: " +
                               source->virtual_path.to_string());
            continue;
        }
        if (!is_valid_relative_path(source->virtual_path.relative_path)) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           "shader_compiler.invalid_source_path",
                           "shader source path is not a safe virtual relative path: " +
                               source->virtual_path.to_string());
            continue;
        }

        auto source_bytes = read_file_bytes(source->source_path, config.maximum_source_bytes);
        if (!source_bytes) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           source_bytes.error().code, source_bytes.error().message);
            continue;
        }
        if (source_bytes.value().empty()) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           "shader_compiler.empty_source",
                           "shader source is empty: " + source->virtual_path.to_string());
            continue;
        }
        if (is_production_profile(config.profile) && language != ShaderSourceLanguage::spirv) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                           "shader_compiler.production_compiler_unavailable",
                           "production shader compilation is not available for " +
                               std::string(shader_source_language_name(language)) +
                               " source yet: " + source->virtual_path.to_string());
            continue;
        }
        if (is_production_profile(config.profile)) {
            auto status =
                validate_spirv_shader_bytes(source_bytes.value(), source->virtual_path.to_string());
            if (!status) {
                add_diagnostic(result, modding::DiagnosticSeverity::error, source->source_path,
                               status.error().code, status.error().message);
                continue;
            }
        }

        auto compiled_path = make_compiled_path(*source, config, role);
        const auto backend = std::string(shader_compile_backend_name(language, config.profile));
        const auto compiled_hash = core::stable_hash64_hex(
            source->logical_id + "|" + source->content_hash + "|" + config.profile + "|" +
            std::to_string(config.pipeline_version) + "|" + backend);
        CompiledShaderRecord record{
            source->logical_id,
            source->virtual_path,
            source->source_kind,
            source->source_id,
            source->content_hash,
            language,
            role,
            compiled_path,
            compiled_hash,
            backend,
            config.pipeline_version,
            source_bytes.value().size(),
        };
        result.records.push_back(record);
        pending_shaders.push_back(
            PendingShader{std::move(record), std::move(source_bytes).value()});
    }

    if (result.has_errors()) {
        return core::Result<ShaderCompileResult>::success(std::move(result));
    }

    for (const auto& shader : pending_shaders) {
        auto compiled_path =
            assets::resolve_asset_path(output_root.value(), shader.record.compiled_relative_path);
        if (!compiled_path) {
            return core::Result<ShaderCompileResult>::failure(compiled_path.error().code,
                                                              compiled_path.error().message);
        }
        auto status =
            write_compiled_payload(compiled_path.value(), shader.record, shader.source_bytes);
        if (!status) {
            return core::Result<ShaderCompileResult>::failure(status.error().code,
                                                              status.error().message);
        }
        ++result.compiled_shader_count;
        result.compiled_payload_bytes += shader.source_bytes.size();
    }

    auto status =
        write_manifest_file(result.manifest_path, encode_manifest(config, result.records));
    if (!status) {
        return core::Result<ShaderCompileResult>::failure(status.error().code,
                                                          status.error().message);
    }

    return core::Result<ShaderCompileResult>::success(std::move(result));
}

ShaderSourceLanguage infer_shader_language(const std::filesystem::path& path) {
    const auto extension = lower_ascii(path.extension().generic_string());
    if (extension == ".slang") {
        return ShaderSourceLanguage::slang;
    }
    if (extension == ".hlsl") {
        return ShaderSourceLanguage::hlsl;
    }
    if (extension == ".spv") {
        return ShaderSourceLanguage::spirv;
    }
    return ShaderSourceLanguage::unknown;
}

ShaderSourceRole infer_shader_role(const std::filesystem::path& path) {
    if (has_path_segment(path, "templates")) {
        return ShaderSourceRole::template_source;
    }
    if (filename_contains(path, ".vert.") || filename_contains(path, "_vert.") ||
        filename_contains(path, ".vertex.") || filename_contains(path, "_vertex.")) {
        return ShaderSourceRole::vertex;
    }
    if (filename_contains(path, ".frag.") || filename_contains(path, "_frag.") ||
        filename_contains(path, ".fragment.") || filename_contains(path, "_fragment.")) {
        return ShaderSourceRole::fragment;
    }
    if (filename_contains(path, ".comp.") || filename_contains(path, "_comp.") ||
        filename_contains(path, ".compute.") || filename_contains(path, "_compute.")) {
        return ShaderSourceRole::compute;
    }
    return ShaderSourceRole::library;
}

std::string_view shader_source_language_name(ShaderSourceLanguage language) noexcept {
    switch (language) {
    case ShaderSourceLanguage::slang:
        return "slang";
    case ShaderSourceLanguage::hlsl:
        return "hlsl";
    case ShaderSourceLanguage::spirv:
        return "spirv";
    case ShaderSourceLanguage::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view shader_source_role_name(ShaderSourceRole role) noexcept {
    switch (role) {
    case ShaderSourceRole::template_source:
        return "template";
    case ShaderSourceRole::vertex:
        return "vertex";
    case ShaderSourceRole::fragment:
        return "fragment";
    case ShaderSourceRole::compute:
        return "compute";
    case ShaderSourceRole::library:
        return "library";
    case ShaderSourceRole::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view shader_compile_backend_name(ShaderSourceLanguage language) noexcept {
    switch (language) {
    case ShaderSourceLanguage::slang:
        return "slang_dev_validation_v1";
    case ShaderSourceLanguage::hlsl:
        return "hlsl_dev_validation_v1";
    case ShaderSourceLanguage::spirv:
        return "spirv_dev_passthrough_v1";
    case ShaderSourceLanguage::unknown:
        return "unknown_shader_backend";
    }
    return "unknown_shader_backend";
}

std::string_view shader_compile_backend_name(ShaderSourceLanguage language,
                                             std::string_view profile) noexcept {
    if (!is_production_profile(profile)) {
        return shader_compile_backend_name(language);
    }

    switch (language) {
    case ShaderSourceLanguage::spirv:
        return "spirv_runtime_passthrough_v1";
    case ShaderSourceLanguage::slang:
        return "slang_spirv_converter_v1";
    case ShaderSourceLanguage::hlsl:
        return "hlsl_spirv_converter_v1";
    case ShaderSourceLanguage::unknown:
        return "unknown_shader_backend";
    }
    return "unknown_shader_backend";
}

} // namespace heartstead::renderer::shaders
