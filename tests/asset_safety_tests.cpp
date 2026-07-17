#include "engine/assets/asset_catalog.hpp"
#include "engine/assets/asset_cooker.hpp"
#include "engine/assets/cooked_asset_manifest.hpp"
#include "engine/assets/cooked_asset_store.hpp"
#include "engine/core/hash.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/renderer/shaders/shader_compiler.hpp"
#include "engine/renderer/shaders/spirv_loader.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace heartstead;

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("heartstead_asset_safety_" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    write_bytes(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

[[nodiscard]] assets::AssetRecord make_record(std::string logical_id, std::string source_id,
                                              std::uint32_t priority,
                                              const std::filesystem::path& source_path,
                                              std::string_view content) {
    auto virtual_path = assets::VirtualPath::parse(logical_id);
    assert(virtual_path);
    return assets::AssetRecord{
        std::move(logical_id),
        assets::AssetKind::data,
        std::move(virtual_path).value(),
        assets::AssetSourceKind::mod,
        std::move(source_id),
        priority,
        source_path,
        core::stable_hash64_hex(content),
        false,
        {},
    };
}

[[nodiscard]] std::vector<std::uint8_t> spirv_bytes(const std::array<std::uint32_t, 5>& words) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * sizeof(std::uint32_t));
    for (const auto word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 16U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 24U) & 0xffU));
    }
    return bytes;
}

void test_catalog_source_identity_and_relative_paths() {
    auto valid = make_record("base:data/value.txt", "base", 0, "value.txt", "value");

    auto traversal = valid;
    traversal.source_id = "../escape";
    assets::AssetCatalog traversal_catalog;
    auto status = traversal_catalog.add(std::move(traversal));
    assert(!status);
    assert(status.error().code == "asset_catalog.invalid_source");

    auto backslash = valid;
    backslash.source_id = "bad\\source";
    assets::AssetCatalog backslash_catalog;
    status = backslash_catalog.add(std::move(backslash));
    assert(!status);
    assert(status.error().code == "asset_catalog.invalid_source");

    assets::AssetCatalog duplicate_catalog;
    assert(duplicate_catalog.add(valid));
    status = duplicate_catalog.add(valid);
    assert(!status);
    assert(status.error().code == "asset_catalog.duplicate_asset");

    assert(assets::is_safe_asset_relative_path("asset_manifest.txt"));
    assert(!assets::is_safe_asset_relative_path("../asset_manifest.txt"));
    assert(!assets::is_safe_asset_relative_path("nested/./asset_manifest.txt"));
    assert(!assets::is_safe_asset_relative_path("nested\\asset_manifest.txt"));
    assert(!assets::is_safe_asset_relative_path("/asset_manifest.txt"));
}

void test_cooker_and_store_containment_and_active_override() {
    TemporaryDirectory temporary;
    const auto inactive_source = temporary.path() / "sources/inactive.txt";
    const auto active_source = temporary.path() / "sources/active.txt";
    write_text(inactive_source, "inactive");
    write_text(active_source, "active");

    assets::AssetCatalog catalog;
    assert(catalog.add(make_record("base:data/value.txt", "base", 0, inactive_source, "inactive")));
    assert(
        catalog.add(make_record("base:data/value.txt", "override", 10, active_source, "active")));

    assets::AssetCookConfig config;
    config.output_root = temporary.path() / "cooked";
    config.manifest_config.active_assets_only = false;
    auto cooked = assets::AssetCooker::cook(catalog, config);
    assert(cooked);
    assert(cooked.value().manifest.records.size() == 2);
    assert(cooked.value().manifest.validate());

    auto store = assets::CookedAssetStore::load(config.output_root);
    assert(store);
    auto payload = store.value().load_payload("base:data/value.txt");
    assert(payload);
    assert(std::string(payload.value().bytes.begin(), payload.value().bytes.end()) == "active");

    const auto* active = cooked.value().manifest.find_active("base:data/value.txt");
    assert(active != nullptr);
    const auto active_payload = config.output_root / active->cooked_relative_path;
    const auto outside_payload = temporary.path() / "outside.cooked";
    std::filesystem::copy_file(active_payload, outside_payload);

    auto hostile_record = *active;
    hostile_record.cooked_relative_path = "../outside.cooked";
    auto escaped = store.value().load_payload(hostile_record);
    assert(!escaped);
    assert(escaped.error().code == "cooked_asset_store.invalid_cooked_path");

    std::filesystem::create_symlink(outside_payload, config.output_root / "payload_link.cooked");
    hostile_record.cooked_relative_path = "payload_link.cooked";
    escaped = store.value().load_payload(hostile_record);
    assert(!escaped);
    assert(escaped.error().code == "asset_path.outside_root");

    auto escaped_manifest =
        assets::CookedAssetStore::load(config.output_root, "../asset_manifest.txt");
    assert(!escaped_manifest);
    assert(escaped_manifest.error().code == "cooked_asset_store.invalid_manifest_path");

    const auto outside_manifest = temporary.path() / "outside_manifest.txt";
    std::filesystem::copy_file(cooked.value().manifest_path, outside_manifest);
    std::filesystem::create_symlink(outside_manifest, config.output_root / "manifest_link.txt");
    escaped_manifest = assets::CookedAssetStore::load(config.output_root, "manifest_link.txt");
    assert(!escaped_manifest);
    assert(escaped_manifest.error().code == "asset_path.outside_root");

    auto traversal_config = config;
    traversal_config.output_root = temporary.path() / "traversal_output";
    traversal_config.manifest_relative_path = "../escaped_manifest.txt";
    assert(!assets::validate_asset_cook_config(traversal_config));
    assert(!assets::AssetCooker::cook(catalog, traversal_config));

    const auto symlink_output = temporary.path() / "symlink_output";
    const auto symlink_escape = temporary.path() / "symlink_escape";
    std::filesystem::create_directories(symlink_output);
    std::filesystem::create_directories(symlink_escape);
    std::filesystem::create_directory_symlink(symlink_escape, symlink_output / "base");
    auto symlink_config = config;
    symlink_config.output_root = symlink_output;
    auto symlink_cook = assets::AssetCooker::cook(catalog, symlink_config);
    assert(!symlink_cook);
    assert(symlink_cook.error().code == "asset_path.outside_root");
    assert(std::filesystem::is_empty(symlink_escape));

    const auto manifest_symlink_output = temporary.path() / "manifest_symlink_output";
    std::filesystem::create_directories(manifest_symlink_output);
    std::filesystem::create_directory_symlink(symlink_escape, manifest_symlink_output / "linked");
    assets::AssetCatalog empty_catalog;
    assets::AssetCookConfig manifest_symlink_config;
    manifest_symlink_config.output_root = manifest_symlink_output;
    manifest_symlink_config.manifest_relative_path = "linked/manifest.txt";
    auto manifest_symlink_cook = assets::AssetCooker::cook(empty_catalog, manifest_symlink_config);
    assert(!manifest_symlink_cook);
    assert(manifest_symlink_cook.error().code == "asset_path.outside_root");
}

void test_manifest_uniqueness_and_identity() {
    TemporaryDirectory temporary;
    const auto first_source = temporary.path() / "first.txt";
    const auto second_source = temporary.path() / "second.txt";
    write_text(first_source, "first");
    write_text(second_source, "second");

    assets::AssetCatalog catalog;
    assert(catalog.add(make_record("base:data/value.txt", "first", 0, first_source, "first")));
    assert(catalog.add(make_record("base:data/value.txt", "second", 1, second_source, "second")));
    auto manifest = assets::CookedAssetManifestBuilder::build(
        catalog, assets::CookedAssetBuildConfig{"development", false, 1});
    assert(manifest);
    assert(manifest.value().records.size() == 2);

    auto duplicate = manifest.value();
    duplicate.records.push_back(duplicate.records.front());
    auto status = duplicate.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.duplicate_record");

    auto multiple_active = manifest.value();
    for (auto& record : multiple_active.records) {
        record.active = true;
    }
    status = multiple_active.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.multiple_active_records");

    auto missing_active = manifest.value();
    for (auto& record : missing_active.records) {
        record.active = false;
    }
    status = missing_active.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.missing_active_record");

    auto duplicate_output = manifest.value();
    duplicate_output.records.back().cooked_relative_path =
        duplicate_output.records.front().cooked_relative_path;
    status = duplicate_output.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.duplicate_cooked_path");

    auto mismatched_identity = manifest.value();
    mismatched_identity.records.front().logical_id = "base:data/other.txt";
    status = mismatched_identity.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.logical_path_mismatch");

    auto malformed_hash = manifest.value();
    malformed_hash.records.front().source_hash = "not-a-digest";
    status = malformed_hash.validate();
    assert(!status);
    assert(status.error().code == "cooked_asset_manifest.invalid_source_hash");
}

void test_shader_output_containment_and_spirv_contract() {
    TemporaryDirectory temporary;
    const auto shader_source = temporary.path() / "shaders/minimal.vert.slang";
    write_text(shader_source, "void main() {}\n");

    assets::AssetCatalog catalog;
    auto shader_record = make_record("base:shaders/minimal.vert.slang", "base", 0, shader_source,
                                     "void main() {}\n");
    shader_record.kind = assets::AssetKind::shader;
    assert(catalog.add(std::move(shader_record)));

    renderer::shaders::ShaderCompileConfig traversal_config;
    traversal_config.output_root = temporary.path() / "shader_traversal";
    traversal_config.manifest_relative_path = "../escaped_shader_manifest.txt";
    auto traversal = renderer::shaders::ShaderCompiler::compile(catalog, traversal_config);
    assert(!traversal);
    assert(traversal.error().code == "shader_compiler.invalid_manifest_path");

    const auto output_root = temporary.path() / "shader_output";
    const auto outside_root = temporary.path() / "shader_escape";
    std::filesystem::create_directories(output_root);
    std::filesystem::create_directories(outside_root);
    std::filesystem::create_directory_symlink(outside_root, output_root / "base");
    renderer::shaders::ShaderCompileConfig symlink_config;
    symlink_config.output_root = output_root;
    auto symlink_compile = renderer::shaders::ShaderCompiler::compile(catalog, symlink_config);
    assert(!symlink_compile);
    assert(symlink_compile.error().code == "asset_path.outside_root");
    assert(std::filesystem::is_empty(outside_root));

    constexpr std::array<std::uint32_t, 5> valid_words{0x07230203, 0x00010600, 0, 1, 0};
    auto version_two_words = valid_words;
    version_two_words[1] = 0x00020000;
    auto nonzero_schema_words = valid_words;
    nonzero_schema_words[4] = 1;
    const auto valid_bytes = spirv_bytes(valid_words);
    const auto version_two_bytes = spirv_bytes(version_two_words);
    const auto nonzero_schema_bytes = spirv_bytes(nonzero_schema_words);

    assert(renderer::shaders::validate_spirv(valid_words));
    assert(renderer::shaders::validate_spirv(valid_bytes));
    assert(!renderer::shaders::validate_spirv(version_two_words));
    assert(!renderer::shaders::validate_spirv(version_two_bytes));
    assert(!renderer::shaders::validate_spirv(nonzero_schema_words));
    assert(!renderer::shaders::validate_spirv(nonzero_schema_bytes));
    auto compiler_validation = renderer::shaders::validate_spirv_shader_bytes(
        version_two_bytes, "base:shaders/version_two.vert.spv");
    assert(!compiler_validation);
    assert(compiler_validation.error().code == "shader_compiler.invalid_spirv");

    renderer::rhi::RenderShaderModuleDesc module_desc;
    module_desc.stage = renderer::rhi::RenderShaderStage::vertex;
    assert(renderer::rhi::validate_render_shader_module_upload(module_desc, valid_words));
    assert(!renderer::rhi::validate_render_shader_module_upload(module_desc, version_two_words));
    assert(!renderer::rhi::validate_render_shader_module_upload(module_desc, nonzero_schema_words));

    const auto invalid_spirv_source = temporary.path() / "version_two.vert.spv";
    write_bytes(invalid_spirv_source, version_two_bytes);
    assets::AssetCatalog spirv_catalog;
    auto spirv_record = make_record("base:shaders/version_two.vert.spv", "base", 0,
                                    invalid_spirv_source, "placeholder");
    spirv_record.kind = assets::AssetKind::shader;
    spirv_record.content_hash =
        core::stable_hash64_hex(std::span<const std::uint8_t>(version_two_bytes));
    assert(spirv_catalog.add(std::move(spirv_record)));

    assets::AssetCookConfig cook_config;
    cook_config.output_root = temporary.path() / "invalid_spirv_cook";
    cook_config.backend = assets::AssetCookBackend::production_converters;
    auto invalid_cook = assets::AssetCooker::cook(spirv_catalog, cook_config);
    assert(!invalid_cook);
    assert(invalid_cook.error().code == "shader_compiler.invalid_spirv");

    renderer::shaders::ShaderCompileConfig compile_config;
    compile_config.output_root = temporary.path() / "invalid_spirv_compile";
    compile_config.profile = "production";
    auto invalid_compile =
        renderer::shaders::ShaderCompiler::compile(spirv_catalog, compile_config);
    assert(invalid_compile);
    assert(invalid_compile.value().has_errors());
    assert(invalid_compile.value().diagnostics.front().code == "shader_compiler.invalid_spirv");
}

} // namespace

int main() {
    test_catalog_source_identity_and_relative_paths();
    test_cooker_and_store_containment_and_active_override();
    test_manifest_uniqueness_and_identity();
    test_shader_output_containment_and_spirv_contract();
    return 0;
}
