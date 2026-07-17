#include "engine/scripting/script_module_loader.hpp"

#include "engine/core/file_io.hpp"
#include "engine/core/filesystem.hpp"
#include "engine/core/ids.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::scripting {

namespace {

[[nodiscard]] bool is_script_task(modding::ModLifecycleTaskKind kind) noexcept {
    using modding::ModLifecycleTaskKind;
    return kind == ModLifecycleTaskKind::runtime_server_script ||
           kind == ModLifecycleTaskKind::runtime_client_script ||
           kind == ModLifecycleTaskKind::migration_script;
}

[[nodiscard]] std::optional<ScriptStage>
script_stage_for_task(modding::ModLifecycleTaskKind kind) noexcept {
    using modding::ModLifecycleTaskKind;
    switch (kind) {
    case ModLifecycleTaskKind::runtime_server_script:
        return ScriptStage::runtime_server;
    case ModLifecycleTaskKind::runtime_client_script:
        return ScriptStage::runtime_client;
    case ModLifecycleTaskKind::migration_script:
        return ScriptStage::migration;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] const modding::ModManifest* find_mod(const std::vector<modding::ModManifest>& mods,
                                                   std::string_view mod_id) noexcept {
    const auto found = std::ranges::find(mods, mod_id, &modding::ModManifest::id);
    return found == mods.end() ? nullptr : &*found;
}

void add_diagnostic(ScriptModuleLoadResult& result, modding::DiagnosticSeverity severity,
                    std::filesystem::path source, std::string code, std::string message) {
    result.diagnostics.push_back(modding::ModDiagnostic{
        severity,
        std::move(source),
        std::move(code),
        std::move(message),
    });
}

[[nodiscard]] bool has_script_extension(const std::filesystem::path& path) {
    const auto extension = path.extension().generic_string();
    return extension == ".lua" || extension == ".luau";
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                              value.back() == '\n')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<std::string> unquoted(std::string_view value) {
    value = trim(value);
    if (value.empty()) {
        return std::string{};
    }
    const auto starts_quoted = value.front() == '"' || value.front() == '\'';
    const auto ends_quoted = value.back() == '"' || value.back() == '\'';
    if (starts_quoted || ends_quoted) {
        if (value.size() < 2 || !starts_quoted || value.back() != value.front()) {
            return std::nullopt;
        }
        const auto quote = value.front();
        value.remove_prefix(1);
        value.remove_suffix(1);
        if (value.find(quote) != std::string_view::npos) {
            return std::nullopt;
        }
    }
    return std::string(value);
}

[[nodiscard]] std::optional<std::string_view> directive_value(std::string_view directive,
                                                              std::string_view key) noexcept {
    if (!starts_with(directive, key)) {
        return std::nullopt;
    }
    directive.remove_prefix(key.size());
    directive = trim(directive);
    if (directive.empty() || (directive.front() != '=' && directive.front() != ':')) {
        return std::nullopt;
    }
    directive.remove_prefix(1);
    return trim(directive);
}

[[nodiscard]] std::vector<std::string_view> split_csv(std::string_view value) {
    std::vector<std::string_view> result;
    while (!value.empty()) {
        const auto comma = value.find(',');
        if (comma == std::string_view::npos) {
            result.push_back(trim(value));
            break;
        }
        result.push_back(trim(value.substr(0, comma)));
        value.remove_prefix(comma + 1);
    }
    return result;
}

[[nodiscard]] bool parse_permissions_directive(ScriptModuleDesc& module, std::string_view raw_value,
                                               ScriptModuleLoadResult& result,
                                               const std::filesystem::path& source,
                                               std::uint32_t maximum_permissions) {
    const auto permissions_text = unquoted(raw_value);
    if (!permissions_text) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.invalid_directive_string",
                       "script permissions directive has mismatched or embedded quotes");
        return false;
    }
    if (trim(*permissions_text).empty()) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.empty_permissions_directive",
                       "script permissions directive must name at least one permission");
        return false;
    }
    if (permissions_text->front() == ',' || permissions_text->back() == ',') {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.empty_permission",
                       "script permissions directive contains an empty permission");
        return false;
    }
    if (static_cast<std::size_t>(std::ranges::count(*permissions_text, ',')) + 1U >
        maximum_permissions) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.too_many_permissions",
                       "script permissions directive exceeds its permission limit");
        return false;
    }

    bool ok = true;
    for (const auto token : split_csv(*permissions_text)) {
        if (token.empty()) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                           "scripting.module.empty_permission",
                           "script permissions directive contains an empty permission");
            ok = false;
            continue;
        }
        const auto permission = script_permission_from_name(token);
        if (!permission) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                           "scripting.module.unknown_permission",
                           "script permissions directive names an unknown permission: " +
                               std::string(token));
            ok = false;
            continue;
        }
        module.permissions.push_back(permission.value());
    }
    return ok;
}

[[nodiscard]] bool parse_api_version_directive(ScriptModuleDesc& module, std::string_view raw_value,
                                               ScriptModuleLoadResult& result,
                                               const std::filesystem::path& source) {
    const auto version_text = unquoted(raw_value);
    if (!version_text) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.invalid_directive_string",
                       "script api_version directive has mismatched or embedded quotes");
        return false;
    }
    const auto version_view = trim(*version_text);
    std::uint32_t version = 0;
    const auto* begin = version_view.data();
    const auto* end = begin + version_view.size();
    const auto [ptr, ec] = std::from_chars(begin, end, version);
    if (ec != std::errc{} || ptr != end || version == 0) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, source,
                       "scripting.module.invalid_api_version_directive",
                       "script api_version directive must be a positive integer");
        return false;
    }
    module.api_version = version;
    return true;
}

[[nodiscard]] bool parse_script_directives(ScriptModuleDesc& module, ScriptModuleLoadResult& result,
                                           const ScriptModuleLoadConfig& config) {
    bool ok = true;
    bool permissions_seen = false;
    bool api_version_seen = false;
    std::uint32_t directive_count = 0;
    std::string sanitized_source = module.source;
    const std::string_view source(module.source);
    std::size_t line_start = 0;
    while (line_start < source.size()) {
        const auto newline = source.find('\n', line_start);
        const auto line_end = newline == std::string_view::npos ? source.size() : newline;
        auto line = source.substr(line_start, line_end - line_start);
        line = trim(line);

        bool is_module_directive = false;
        if (starts_with(line, "--")) {
            line.remove_prefix(2);
            line = trim(line);
            if (starts_with(line, "heartstead.")) {
                is_module_directive = true;
                ++directive_count;
                if (directive_count > config.max_directives_per_module) {
                    add_diagnostic(result, modding::DiagnosticSeverity::error, module.source_path,
                                   "scripting.module.too_many_directives",
                                   "script module exceeds its directive limit");
                    return false;
                }
                if (const auto permissions_value =
                        directive_value(line, "heartstead.permissions")) {
                    if (permissions_seen) {
                        add_diagnostic(result, modding::DiagnosticSeverity::error,
                                       module.source_path,
                                       "scripting.module.duplicate_permissions_directive",
                                       "script module repeats its permissions directive");
                        ok = false;
                    } else {
                        permissions_seen = true;
                        ok = parse_permissions_directive(module, permissions_value.value(), result,
                                                         module.source_path,
                                                         config.max_permissions_per_module) &&
                             ok;
                    }
                } else if (const auto api_version_value =
                               directive_value(line, "heartstead.api_version")) {
                    if (api_version_seen) {
                        add_diagnostic(result, modding::DiagnosticSeverity::error,
                                       module.source_path,
                                       "scripting.module.duplicate_api_version_directive",
                                       "script module repeats its api_version directive");
                        ok = false;
                    } else {
                        api_version_seen = true;
                        ok = parse_api_version_directive(module, api_version_value.value(), result,
                                                         module.source_path) &&
                             ok;
                    }
                } else {
                    add_diagnostic(result, modding::DiagnosticSeverity::warning, module.source_path,
                                   "scripting.module.unknown_directive",
                                   "script heartstead directive is not recognized");
                }
            }
        }

        if (is_module_directive) {
            std::fill(sanitized_source.begin() + static_cast<std::ptrdiff_t>(line_start),
                      sanitized_source.begin() + static_cast<std::ptrdiff_t>(line_end), ' ');
        }
        if (newline == std::string_view::npos) {
            break;
        }
        line_start = newline + 1;
    }
    module.source = std::move(sanitized_source);
    return ok;
}

} // namespace

bool ScriptModuleLoadResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const modding::ModDiagnostic& diagnostic) {
        return diagnostic.severity == modding::DiagnosticSeverity::error;
    });
}

std::size_t ScriptModuleLoadResult::count_stage(ScriptStage stage) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        modules, [stage](const ScriptModuleDesc& module) { return module.stage == stage; }));
}

ScriptModuleLoadResult
ScriptModuleLoader::load_from_plan(const std::vector<modding::ModManifest>& mods,
                                   const modding::ModLifecyclePlan& lifecycle_plan,
                                   ScriptModuleLoadConfig config) {
    ScriptModuleLoadResult result;
    constexpr std::uint32_t known_permission_count = 6;
    if (config.max_source_bytes == 0 || config.max_modules == 0 ||
        config.max_directives_per_module == 0 || config.max_permissions_per_module == 0) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, {},
                       "scripting.module.invalid_loader_limits",
                       "script module loader limits must be non-zero");
        return result;
    }
    if (config.max_permissions_per_module > known_permission_count) {
        add_diagnostic(result, modding::DiagnosticSeverity::error, {},
                       "scripting.module.invalid_loader_limits",
                       "script module permission limit exceeds the known permission set");
        return result;
    }

    std::set<std::string> seen_module_ids;
    std::uint32_t script_task_count = 0;

    for (const auto& task : lifecycle_plan.tasks) {
        if (!is_script_task(task.kind)) {
            continue;
        }
        ++script_task_count;
        if (script_task_count > config.max_modules) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.module_limit_reached",
                           "script lifecycle plan exceeds the module loader limit");
            break;
        }

        const auto* mod = find_mod(mods, task.mod_id);
        if (mod == nullptr) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.unknown_mod",
                           "script lifecycle task references an unknown mod: " + task.mod_id);
            continue;
        }

        if (!has_script_extension(task.source)) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.unsupported_extension",
                           "script module source must use .lua or .luau");
            continue;
        }

        const auto stage = script_stage_for_task(task.kind);
        if (!stage) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.invalid_lifecycle_task",
                           "script lifecycle task has no matching script stage");
            continue;
        }
        auto relative_source = core::relative_path_below(mod->root, task.source);
        if (!relative_source) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.unsafe_source_path", relative_source.error().message);
            continue;
        }
        relative_source.value().replace_extension();
        const auto module_id = mod->id + ":" + relative_source.value().generic_string();
        if (!core::PrototypeId::parse(module_id)) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.invalid_module_id",
                           "script lifecycle source does not produce a valid module id");
            continue;
        }
        if (!seen_module_ids.insert(module_id).second) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           "scripting.module.duplicate_id",
                           "multiple script files produce the same module id: " + module_id);
            continue;
        }

        auto source = core::read_text_file(task.source, {.maximum_bytes = config.max_source_bytes});
        if (!source) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           source.error().code == "core.file_too_large"
                               ? "scripting.source_too_large"
                               : "scripting.module.read_failed",
                           source.error().message);
            continue;
        }

        ScriptModuleDesc module;
        module.module_id = module_id;
        module.source_mod_id = mod->id;
        module.source_path = task.source;
        module.source = std::move(source).value();
        module.stage = stage.value();

        if (!parse_script_directives(module, result, config)) {
            continue;
        }

        auto status = validate_script_module_desc(module, config.max_source_bytes);
        if (!status) {
            add_diagnostic(result, modding::DiagnosticSeverity::error, task.source,
                           status.error().code, status.error().message);
            continue;
        }

        result.modules.push_back(std::move(module));
    }

    std::ranges::sort(result.modules, {}, &ScriptModuleDesc::module_id);
    return result;
}

} // namespace heartstead::scripting
