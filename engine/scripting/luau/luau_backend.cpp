#include "engine/scripting/luau/luau_backend.hpp"

#include "engine/core/ids.hpp"

#include <cctype>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace heartstead::scripting::luau {

namespace {

enum class ReturnExpressionKind {
    nil_value,
    boolean_value,
    number_value,
    string_value,
    argument,
    emit_event,
};

struct EmitArgumentExpression {
    bool from_parameter = false;
    ScriptValue literal;
    std::string parameter_name;
};

struct ReturnExpression {
    ReturnExpressionKind kind = ReturnExpressionKind::nil_value;
    ScriptValue literal;
    std::string argument_name;
    std::string emitted_event;
    std::vector<EmitArgumentExpression> emitted_arguments;
};

struct ExportedFunction {
    std::string name;
    std::vector<std::string> parameters;
    ReturnExpression return_expression;
    std::uint32_t instruction_estimate = 8;
};

struct ModuleRecord {
    ScriptModuleInfo info;
    std::unordered_map<std::string, ExportedFunction> functions;
};

[[nodiscard]] bool is_identifier_start(char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

[[nodiscard]] bool is_identifier_continue(char character) noexcept {
    return is_identifier_start(character) || (character >= '0' && character <= '9');
}

[[nodiscard]] bool is_valid_export_name(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    bool expect_segment_start = true;
    for (const auto character : value) {
        if (character == '.') {
            if (expect_segment_start) {
                return false;
            }
            expect_segment_start = true;
            continue;
        }
        if (expect_segment_start) {
            if (!is_identifier_start(character)) {
                return false;
            }
            expect_segment_start = false;
        } else if (!is_identifier_continue(character)) {
            return false;
        }
    }
    return !expect_segment_start;
}

[[nodiscard]] bool is_valid_emitted_event_name(std::string_view value) noexcept {
    if (value.empty() || value.front() == '.' || value.back() == '.') {
        return false;
    }

    for (const auto character : value) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ScriptModuleInfo make_module_info(const ScriptModuleDesc& desc) {
    return ScriptModuleInfo{desc.module_id,   desc.source_mod_id, desc.source_path, desc.stage,
                            desc.api_version, desc.source.size(), desc.permissions};
}

class Parser {
  public:
    explicit Parser(std::string_view source) : source_(source) {}

    [[nodiscard]] core::Result<std::unordered_map<std::string, ExportedFunction>> parse_module() {
        skip_whitespace();
        auto status = expect_keyword("return");
        if (!status) {
            return failure(status);
        }
        status = expect_char('{');
        if (!status) {
            return failure(status);
        }

        std::unordered_map<std::string, ExportedFunction> functions;
        skip_whitespace();
        while (!consume_char('}')) {
            auto function = parse_function_entry();
            if (!function) {
                return failure(function.error().code, function.error().message);
            }
            auto parsed_function = std::move(function).value();
            const auto function_name = parsed_function.name;
            const auto [_, inserted] = functions.emplace(function_name, std::move(parsed_function));
            if (!inserted) {
                return failure("scripting.luau_duplicate_function",
                               "script module exports the same function more than once");
            }

            skip_whitespace();
            if (consume_char(',') || consume_char(';')) {
                skip_whitespace();
                continue;
            }
            if (!peek_char('}')) {
                return failure("scripting.luau_parse_error",
                               "expected comma, semicolon, or closing table brace");
            }
        }

        skip_whitespace();
        if (!is_at_end()) {
            return failure("scripting.luau_parse_error",
                           "unexpected source after exported function table");
        }
        if (functions.empty()) {
            return failure("scripting.luau_no_exports",
                           "script module must export at least one function");
        }
        return core::Result<std::unordered_map<std::string, ExportedFunction>>::success(
            std::move(functions));
    }

  private:
    [[nodiscard]] core::Result<std::unordered_map<std::string, ExportedFunction>>
    failure(const core::Status& status) const {
        return failure(status.error().code, status.error().message);
    }

    [[nodiscard]] core::Result<std::unordered_map<std::string, ExportedFunction>>
    failure(std::string code, std::string message) const {
        return core::Result<std::unordered_map<std::string, ExportedFunction>>::failure(
            std::move(code), std::move(message));
    }

    [[nodiscard]] core::Result<ExportedFunction> parse_function_entry() {
        auto name = parse_export_name();
        if (!name) {
            return core::Result<ExportedFunction>::failure(name.error().code, name.error().message);
        }
        auto status = expect_char('=');
        if (!status) {
            return core::Result<ExportedFunction>::failure(status.error().code,
                                                           status.error().message);
        }
        status = expect_keyword("function");
        if (!status) {
            return core::Result<ExportedFunction>::failure(status.error().code,
                                                           status.error().message);
        }
        status = expect_char('(');
        if (!status) {
            return core::Result<ExportedFunction>::failure(status.error().code,
                                                           status.error().message);
        }
        auto parameters = parse_parameters();
        if (!parameters) {
            return core::Result<ExportedFunction>::failure(parameters.error().code,
                                                           parameters.error().message);
        }
        status = expect_keyword("return");
        if (!status) {
            return core::Result<ExportedFunction>::failure(status.error().code,
                                                           status.error().message);
        }
        auto expression = parse_return_expression();
        if (!expression) {
            return core::Result<ExportedFunction>::failure(expression.error().code,
                                                           expression.error().message);
        }
        if (expression.value().kind == ReturnExpressionKind::argument) {
            bool known_parameter = false;
            for (const auto& parameter : parameters.value()) {
                if (parameter == expression.value().argument_name) {
                    known_parameter = true;
                    break;
                }
            }
            if (!known_parameter) {
                return core::Result<ExportedFunction>::failure(
                    "scripting.luau_unknown_parameter",
                    "script function returns an unknown parameter");
            }
        }
        if (expression.value().kind == ReturnExpressionKind::emit_event) {
            for (const auto& argument : expression.value().emitted_arguments) {
                if (!argument.from_parameter) {
                    continue;
                }
                bool known_parameter = false;
                for (const auto& parameter : parameters.value()) {
                    if (parameter == argument.parameter_name) {
                        known_parameter = true;
                        break;
                    }
                }
                if (!known_parameter) {
                    return core::Result<ExportedFunction>::failure(
                        "scripting.luau_unknown_parameter",
                        "script function emits an unknown parameter");
                }
            }
        }
        status = expect_keyword("end");
        if (!status) {
            return core::Result<ExportedFunction>::failure(status.error().code,
                                                           status.error().message);
        }

        ExportedFunction function;
        function.name = std::move(name).value();
        function.parameters = std::move(parameters).value();
        function.return_expression = std::move(expression).value();
        function.instruction_estimate =
            static_cast<std::uint32_t>(8 + function.parameters.size() * 2);
        if (function.return_expression.kind == ReturnExpressionKind::emit_event) {
            function.instruction_estimate += static_cast<std::uint32_t>(
                4 + function.return_expression.emitted_arguments.size() * 2);
        }
        return core::Result<ExportedFunction>::success(std::move(function));
    }

    [[nodiscard]] core::Result<std::string> parse_export_name() {
        auto first = parse_identifier();
        if (!first) {
            return first;
        }

        std::string name = std::move(first).value();
        while (consume_char('.')) {
            auto segment = parse_identifier();
            if (!segment) {
                return core::Result<std::string>::failure("scripting.luau_parse_error",
                                                          "expected identifier after dot");
            }
            name.push_back('.');
            name.append(std::move(segment).value());
        }

        if (!is_valid_export_name(name)) {
            return core::Result<std::string>::failure("scripting.invalid_function_name",
                                                      "script export function name is invalid");
        }
        return core::Result<std::string>::success(std::move(name));
    }

    [[nodiscard]] core::Result<std::vector<std::string>> parse_parameters() {
        std::vector<std::string> parameters;
        skip_whitespace();
        if (consume_char(')')) {
            return core::Result<std::vector<std::string>>::success(std::move(parameters));
        }

        while (true) {
            auto parameter = parse_identifier();
            if (!parameter) {
                return core::Result<std::vector<std::string>>::failure(parameter.error().code,
                                                                       parameter.error().message);
            }
            for (const auto& existing : parameters) {
                if (existing == parameter.value()) {
                    return core::Result<std::vector<std::string>>::failure(
                        "scripting.luau_duplicate_parameter",
                        "script function declares a duplicate parameter");
                }
            }
            parameters.push_back(std::move(parameter).value());

            skip_whitespace();
            if (consume_char(')')) {
                break;
            }
            auto status = expect_char(',');
            if (!status) {
                return core::Result<std::vector<std::string>>::failure(status.error().code,
                                                                       status.error().message);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(parameters));
    }

    [[nodiscard]] core::Result<ReturnExpression> parse_return_expression() {
        skip_whitespace();
        if (consume_keyword("nil")) {
            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::nil_value;
            expression.literal = ScriptValue::nil();
            return core::Result<ReturnExpression>::success(std::move(expression));
        }
        if (consume_keyword("true")) {
            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::boolean_value;
            expression.literal = ScriptValue::boolean(true);
            return core::Result<ReturnExpression>::success(std::move(expression));
        }
        if (consume_keyword("false")) {
            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::boolean_value;
            expression.literal = ScriptValue::boolean(false);
            return core::Result<ReturnExpression>::success(std::move(expression));
        }
        if (peek_char('"') || peek_char('\'')) {
            auto value = parse_string_literal();
            if (!value) {
                return core::Result<ReturnExpression>::failure(value.error().code,
                                                               value.error().message);
            }
            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::string_value;
            expression.literal = ScriptValue::string(std::move(value).value());
            return core::Result<ReturnExpression>::success(std::move(expression));
        }
        if (peek_number_start()) {
            auto value = parse_number_literal();
            if (!value) {
                return core::Result<ReturnExpression>::failure(value.error().code,
                                                               value.error().message);
            }
            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::number_value;
            expression.literal = ScriptValue::number(value.value());
            return core::Result<ReturnExpression>::success(std::move(expression));
        }

        auto identifier = parse_identifier();
        if (!identifier) {
            return core::Result<ReturnExpression>::failure(
                "scripting.luau_parse_error",
                "return expression must be nil, boolean, number, string, parameter, or emit");
        }
        const auto identifier_name = std::move(identifier).value();
        if (identifier_name == "emit" && consume_char('(')) {
            auto event_name = parse_string_literal();
            if (!event_name) {
                return core::Result<ReturnExpression>::failure(event_name.error().code,
                                                               event_name.error().message);
            }
            std::vector<EmitArgumentExpression> emitted_arguments;
            skip_whitespace();
            while (consume_char(',')) {
                auto argument = parse_emit_argument();
                if (!argument) {
                    return core::Result<ReturnExpression>::failure(argument.error().code,
                                                                   argument.error().message);
                }
                emitted_arguments.push_back(std::move(argument).value());
                skip_whitespace();
            }
            auto status = expect_char(')');
            if (!status) {
                return core::Result<ReturnExpression>::failure(status.error().code,
                                                               status.error().message);
            }
            if (!is_valid_emitted_event_name(event_name.value())) {
                return core::Result<ReturnExpression>::failure(
                    "scripting.luau_invalid_event",
                    "emitted event names must contain lowercase letters, digits, underscores, "
                    "dashes, or dots and cannot start or end with a dot");
            }

            ReturnExpression expression;
            expression.kind = ReturnExpressionKind::emit_event;
            expression.emitted_event = std::move(event_name).value();
            expression.emitted_arguments = std::move(emitted_arguments);
            return core::Result<ReturnExpression>::success(std::move(expression));
        }

        ReturnExpression expression;
        expression.kind = ReturnExpressionKind::argument;
        expression.argument_name = identifier_name;
        return core::Result<ReturnExpression>::success(std::move(expression));
    }

    [[nodiscard]] core::Result<EmitArgumentExpression> parse_emit_argument() {
        skip_whitespace();
        if (consume_keyword("true")) {
            EmitArgumentExpression argument;
            argument.literal = ScriptValue::boolean(true);
            return core::Result<EmitArgumentExpression>::success(std::move(argument));
        }
        if (consume_keyword("false")) {
            EmitArgumentExpression argument;
            argument.literal = ScriptValue::boolean(false);
            return core::Result<EmitArgumentExpression>::success(std::move(argument));
        }
        if (peek_char('"') || peek_char('\'')) {
            auto value = parse_string_literal();
            if (!value) {
                return core::Result<EmitArgumentExpression>::failure(value.error().code,
                                                                     value.error().message);
            }
            EmitArgumentExpression argument;
            argument.literal = ScriptValue::string(std::move(value).value());
            return core::Result<EmitArgumentExpression>::success(std::move(argument));
        }
        if (peek_number_start()) {
            auto value = parse_number_literal();
            if (!value) {
                return core::Result<EmitArgumentExpression>::failure(value.error().code,
                                                                     value.error().message);
            }
            EmitArgumentExpression argument;
            argument.literal = ScriptValue::number(value.value());
            return core::Result<EmitArgumentExpression>::success(std::move(argument));
        }

        auto identifier = parse_identifier();
        if (!identifier) {
            return core::Result<EmitArgumentExpression>::failure(
                "scripting.luau_parse_error",
                "emit arguments must be boolean, number, string, or parameter values");
        }
        if (identifier.value() == "nil") {
            return core::Result<EmitArgumentExpression>::failure("scripting.luau_parse_error",
                                                                 "emit arguments cannot be nil");
        }

        EmitArgumentExpression argument;
        argument.from_parameter = true;
        argument.parameter_name = std::move(identifier).value();
        return core::Result<EmitArgumentExpression>::success(std::move(argument));
    }

    [[nodiscard]] core::Result<std::string> parse_identifier() {
        skip_whitespace();
        if (is_at_end() || !is_identifier_start(source_[position_])) {
            return core::Result<std::string>::failure("scripting.luau_parse_error",
                                                      "expected identifier");
        }
        const auto start = position_;
        ++position_;
        while (!is_at_end() && is_identifier_continue(source_[position_])) {
            ++position_;
        }
        return core::Result<std::string>::success(
            std::string(source_.substr(start, position_ - start)));
    }

    [[nodiscard]] core::Result<std::string> parse_string_literal() {
        skip_whitespace();
        if (is_at_end() || (source_[position_] != '"' && source_[position_] != '\'')) {
            return core::Result<std::string>::failure("scripting.luau_parse_error",
                                                      "expected string literal");
        }
        const auto quote = source_[position_];
        ++position_;

        std::string result;
        while (!is_at_end()) {
            const auto character = source_[position_++];
            if (character == quote) {
                return core::Result<std::string>::success(std::move(result));
            }
            if (character == '\\') {
                if (is_at_end()) {
                    break;
                }
                const auto escaped = source_[position_++];
                switch (escaped) {
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case '\\':
                case '"':
                case '\'':
                    result.push_back(escaped);
                    break;
                default:
                    return core::Result<std::string>::failure(
                        "scripting.luau_parse_error", "unsupported string escape in script source");
                }
            } else {
                result.push_back(character);
            }
        }
        return core::Result<std::string>::failure("scripting.luau_parse_error",
                                                  "unterminated string literal");
    }

    [[nodiscard]] core::Result<double> parse_number_literal() {
        skip_whitespace();
        const auto start = position_;
        if (!is_at_end() && (source_[position_] == '-' || source_[position_] == '+')) {
            ++position_;
        }
        while (!is_at_end() && (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
                                source_[position_] == '.')) {
            ++position_;
        }
        if (!is_at_end() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (!is_at_end() && (source_[position_] == '-' || source_[position_] == '+')) {
                ++position_;
            }
            while (!is_at_end() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        try {
            std::size_t consumed = 0;
            const auto number_text = std::string(source_.substr(start, position_ - start));
            const auto value = std::stod(number_text, &consumed);
            if (consumed != number_text.size() || !std::isfinite(value)) {
                return core::Result<double>::failure("scripting.luau_parse_error",
                                                     "invalid numeric literal in script source");
            }
            return core::Result<double>::success(value);
        } catch (...) {
            return core::Result<double>::failure("scripting.luau_parse_error",
                                                 "invalid numeric literal in script source");
        }
    }

    [[nodiscard]] bool peek_number_start() const noexcept {
        if (is_at_end()) {
            return false;
        }
        const auto current = source_[position_];
        return std::isdigit(static_cast<unsigned char>(current)) || current == '-' ||
               current == '+';
    }

    [[nodiscard]] bool consume_keyword(std::string_view keyword) {
        skip_whitespace();
        if (source_.substr(position_, keyword.size()) != keyword) {
            return false;
        }
        const auto after = position_ + keyword.size();
        if (after < source_.size() && is_identifier_continue(source_[after])) {
            return false;
        }
        position_ = after;
        return true;
    }

    [[nodiscard]] core::Status expect_keyword(std::string_view keyword) {
        if (!consume_keyword(keyword)) {
            return core::Status::failure("scripting.luau_parse_error",
                                         "expected keyword: " + std::string(keyword));
        }
        return core::Status::ok();
    }

    [[nodiscard]] bool consume_char(char expected) {
        skip_whitespace();
        if (is_at_end() || source_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] core::Status expect_char(char expected) {
        if (!consume_char(expected)) {
            return core::Status::failure("scripting.luau_parse_error",
                                         std::string("expected character: ") + expected);
        }
        return core::Status::ok();
    }

    [[nodiscard]] bool peek_char(char expected) {
        skip_whitespace();
        return !is_at_end() && source_[position_] == expected;
    }

    void skip_whitespace() noexcept {
        while (!is_at_end() && std::isspace(static_cast<unsigned char>(source_[position_]))) {
            ++position_;
        }
    }

    [[nodiscard]] bool is_at_end() const noexcept {
        return position_ >= source_.size();
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

[[nodiscard]] ScriptCallResult
evaluate_return_expression(const ExportedFunction& function,
                           const std::vector<ScriptValue>& arguments) {
    ScriptCallResult result;

    switch (function.return_expression.kind) {
    case ReturnExpressionKind::nil_value:
    case ReturnExpressionKind::boolean_value:
    case ReturnExpressionKind::number_value:
    case ReturnExpressionKind::string_value:
        result.return_value = function.return_expression.literal;
        return result;
    case ReturnExpressionKind::argument:
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (function.parameters[index] == function.return_expression.argument_name) {
                result.return_value = arguments[index];
                return result;
            }
        }
        result.return_value = ScriptValue::nil();
        return result;
    case ReturnExpressionKind::emit_event:
        result.return_value = ScriptValue::nil();
        ScriptEmittedEvent event;
        event.api_id = function.return_expression.emitted_event;
        event.arguments.reserve(function.return_expression.emitted_arguments.size());
        for (const auto& emitted_argument : function.return_expression.emitted_arguments) {
            if (!emitted_argument.from_parameter) {
                event.arguments.push_back(emitted_argument.literal);
                continue;
            }
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (function.parameters[index] == emitted_argument.parameter_name) {
                    event.arguments.push_back(arguments[index]);
                    break;
                }
            }
        }
        result.emitted_events.push_back(std::move(event));
        return result;
    }

    result.return_value = ScriptValue::nil();
    return result;
}

class LuauFoundationRuntime final : public IScriptRuntime {
  public:
    explicit LuauFoundationRuntime(ScriptRuntimeDesc desc) : desc_(desc) {}

    [[nodiscard]] ScriptBackend backend() const noexcept override {
        return ScriptBackend::luau;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return script_backend_name(ScriptBackend::luau);
    }

    [[nodiscard]] std::size_t module_count() const noexcept override {
        return modules_.size();
    }

    [[nodiscard]] const ScriptModuleInfo*
    find_module(std::string_view module_id) const noexcept override {
        const auto found = modules_.find(std::string(module_id));
        return found == modules_.end() ? nullptr : &found->second.info;
    }

    [[nodiscard]] core::Status load_module(ScriptModuleDesc desc) override {
        auto status = validate_script_module_desc(desc, desc_.max_source_bytes);
        if (!status) {
            return status;
        }
        if (modules_.contains(desc.module_id)) {
            return core::Status::failure("scripting.duplicate_module",
                                         "script module is already loaded");
        }
        if (modules_.size() >= desc_.max_modules) {
            return core::Status::failure("scripting.module_limit_reached",
                                         "script runtime module limit has been reached");
        }

        Parser parser(desc.source);
        auto functions = parser.parse_module();
        if (!functions) {
            return core::Status::failure(functions.error().code, functions.error().message);
        }

        ModuleRecord record;
        record.info = make_module_info(desc);
        record.functions = std::move(functions).value();
        modules_.emplace(desc.module_id, std::move(record));
        return core::Status::ok();
    }

    [[nodiscard]] core::Status unload_module(std::string_view module_id) override {
        if (!core::PrototypeId::parse(module_id)) {
            return core::Status::failure("scripting.invalid_module_id",
                                         "script module id must be namespace:local_id");
        }
        if (modules_.erase(std::string(module_id)) == 0) {
            return core::Status::failure("scripting.module_not_loaded",
                                         "script module is not loaded");
        }
        return core::Status::ok();
    }

    [[nodiscard]] core::Result<ScriptCallResult> call(ScriptCallDesc desc) override {
        auto status = validate_script_call_desc(desc, desc_);
        if (!status) {
            return core::Result<ScriptCallResult>::failure(status.error().code,
                                                           status.error().message);
        }

        const auto module = modules_.find(desc.module_id);
        if (module == modules_.end()) {
            return core::Result<ScriptCallResult>::failure("scripting.module_not_loaded",
                                                           "script module is not loaded");
        }
        if (module->second.info.stage != desc.stage) {
            return core::Result<ScriptCallResult>::failure(
                "scripting.stage_mismatch", "script call stage does not match loaded module stage");
        }
        auto permissions_status =
            validate_script_call_permissions(module->second.info, desc.required_permissions);
        if (!permissions_status) {
            return core::Result<ScriptCallResult>::failure(permissions_status.error().code,
                                                           permissions_status.error().message);
        }

        const auto function = module->second.functions.find(desc.function_name);
        if (function == module->second.functions.end()) {
            return core::Result<ScriptCallResult>::failure(
                "scripting.function_not_found", "script module does not export requested function");
        }
        if (desc.arguments.size() != function->second.parameters.size()) {
            return core::Result<ScriptCallResult>::failure(
                "scripting.argument_count_mismatch",
                "script function call does not match exported parameter count");
        }
        if (desc.instruction_budget < function->second.instruction_estimate) {
            return core::Result<ScriptCallResult>::failure(
                "scripting.instruction_budget_exceeded",
                "script call exceeded its instruction budget");
        }
        auto result = evaluate_return_expression(function->second, desc.arguments);
        status = validate_script_call_result(module->second.info, result, desc_);
        if (!status) {
            return core::Result<ScriptCallResult>::failure(status.error().code,
                                                           status.error().message);
        }
        result.consumed_instruction_estimate = function->second.instruction_estimate;
        return core::Result<ScriptCallResult>::success(std::move(result));
    }

  private:
    ScriptRuntimeDesc desc_;
    std::unordered_map<std::string, ModuleRecord> modules_;
};

} // namespace

ScriptBackendInfo backend_info() noexcept {
    return ScriptBackendInfo{
        ScriptBackend::luau,
        script_backend_name(ScriptBackend::luau),
        true,
        "restricted foundation runtime available; full Luau VM is not linked yet",
    };
}

core::Result<std::unique_ptr<IScriptRuntime>> create_runtime(ScriptRuntimeDesc desc) {
    return core::Result<std::unique_ptr<IScriptRuntime>>::success(
        std::make_unique<LuauFoundationRuntime>(desc));
}

} // namespace heartstead::scripting::luau
