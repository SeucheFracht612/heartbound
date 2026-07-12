#include "engine/net/server_command.hpp"

#include "engine/world/world_state.hpp"

#include <utility>

namespace heartstead::net {

namespace {

[[nodiscard]] bool is_valid_command_type(std::string_view type) noexcept {
    if (type.empty() || type.front() == '.' || type.back() == '.') {
        return false;
    }

    for (const auto character : type) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] CommandOperationTrace trace_from_operation(const world::WorldOperation& operation) {
    CommandOperationTrace trace;
    trace.stages = operation.stages();
    trace.mutations = operation.mutations();
    trace.derived_updates = operation.derived_updates();
    trace.replication_dirty = operation.replication_dirty();
    trace.save_dirty = operation.save_dirty();
    return trace;
}

[[nodiscard]] CommandDispatchReport failed_report(std::uint64_t sequence, std::string command_type,
                                                  std::string code, std::string message) {
    CommandDispatchReport report;
    report.sequence = sequence;
    report.command_type = std::move(command_type);
    report.error = core::Error{std::move(code), std::move(message)};
    return report;
}

[[nodiscard]] CommandDispatchResult result_from_report(const CommandDispatchReport& report) {
    CommandDispatchResult result;
    result.sequence = report.sequence;
    result.command_type = report.command_type;
    result.committed_world_mutation = report.committed_world_mutation;
    result.events = report.events;
    result.reserved_ids = report.reserved_ids;
    result.operation_trace = report.operation_trace;
    return result;
}

} // namespace

core::Status ServerCommandDispatcher::register_command(CommandDescriptor descriptor) {
    if (!is_valid_command_type(descriptor.type)) {
        return core::Status::failure(
            "command.invalid_type",
            "command type must contain lowercase letters, digits, underscores, dashes, or dots");
    }
    if (!descriptor.handler) {
        return core::Status::failure("command.missing_handler", "command handler is required");
    }

    const auto [_, inserted] = commands_.emplace(descriptor.type, std::move(descriptor));
    if (!inserted) {
        return core::Status::failure("command.duplicate_type",
                                     "command type is already registered");
    }

    return core::Status::ok();
}

core::Result<CommandDispatchResult>
ServerCommandDispatcher::dispatch(const CommandEnvelope& envelope,
                                  const CommandExecutionContext& context) const {
    auto report = dispatch_report(envelope, context);
    if (!report.succeeded) {
        return core::Result<CommandDispatchResult>::failure(report.error->code,
                                                            report.error->message);
    }
    return core::Result<CommandDispatchResult>::success(result_from_report(report));
}

CommandDispatchReport
ServerCommandDispatcher::dispatch_report(const CommandEnvelope& envelope,
                                         const CommandExecutionContext& context) const {
    const auto found = commands_.find(envelope.type);
    if (found == commands_.end()) {
        return failed_report(envelope.sequence, envelope.type, "command.unknown_type",
                             "command type is not registered: " + envelope.type);
    }

    const auto& descriptor = found->second;
    if (descriptor.requires_authoritative_server &&
        context.executor_role != CommandExecutorRole::authoritative_server) {
        return failed_report(envelope.sequence, envelope.type, "command.not_authoritative",
                             "mutating command must execute on the authoritative server");
    }

    // Mutating handlers execute against a staged value copy.  WorldOperation used to describe a
    // rollback without undoing mutations that a handler had already made, which made a late
    // validation/derived-update failure observable in the authoritative world.  Keep the public
    // handler contract small while making the transaction real; a future write-set transaction
    // can replace this correctness-first staging without changing commands.
    std::optional<world::WorldState> staged_world;
    CommandExecutionContext staged_context = context;
    if (descriptor.mutates_world && context.world_state != nullptr) {
        staged_world.emplace(*context.world_state);
        staged_context.world_state = &*staged_world;
        if (context.save_ids == &context.world_state->save_ids()) {
            staged_context.save_ids = &staged_world->save_ids();
        }
    }

    std::optional<save::SaveIdAllocator> external_save_ids_before;
    if (descriptor.mutates_world && context.save_ids != nullptr &&
        (context.world_state == nullptr || context.save_ids != &context.world_state->save_ids())) {
        external_save_ids_before = *context.save_ids;
    }
    const auto restore_external_ids = [&]() {
        if (external_save_ids_before.has_value()) {
            *context.save_ids = *external_save_ids_before;
        }
    };

    world::WorldOperation operation(envelope.type);
    const auto validation = operation.validate(true, "registered command");
    if (!validation) {
        auto report = failed_report(envelope.sequence, envelope.type, validation.error().code,
                                    validation.error().message);
        report.operation_trace = trace_from_operation(operation);
        return report;
    }

    const auto handler_status = descriptor.handler(envelope, staged_context, operation);
    if (!handler_status) {
        restore_external_ids();
        operation.rollback(handler_status.error().message);
        auto report = failed_report(envelope.sequence, envelope.type, handler_status.error().code,
                                    handler_status.error().message);
        report.operation_trace = trace_from_operation(operation);
        report.events = operation.events();
        report.reserved_ids = operation.reserved_ids();
        return report;
    }

    CommandDispatchReport report;
    report.sequence = envelope.sequence;
    report.command_type = envelope.type;

    if (descriptor.mutates_world) {
        const auto commit_status = operation.commit();
        if (!commit_status) {
            restore_external_ids();
            report.error = core::Error{commit_status.error().code, commit_status.error().message};
            report.operation_trace = trace_from_operation(operation);
            report.events = operation.events();
            report.reserved_ids = operation.reserved_ids();
            return report;
        }
        if (staged_world.has_value()) {
            *context.world_state = std::move(*staged_world);
        }
        report.committed_world_mutation = true;
    }

    report.succeeded = true;
    report.events = operation.events();
    report.reserved_ids = operation.reserved_ids();
    report.operation_trace = trace_from_operation(operation);
    return report;
}

bool ServerCommandDispatcher::contains(std::string_view type) const {
    return commands_.contains(std::string(type));
}

std::size_t ServerCommandDispatcher::size() const noexcept {
    return commands_.size();
}

} // namespace heartstead::net
