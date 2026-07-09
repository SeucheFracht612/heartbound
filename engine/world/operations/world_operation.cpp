#include "engine/world/operations/world_operation.hpp"

#include <stdexcept>
#include <utility>

namespace heartstead::world {

std::string_view operation_stage_name(OperationStage stage) noexcept {
    switch (stage) {
    case OperationStage::begun:
        return "begun";
    case OperationStage::validated:
        return "validated";
    case OperationStage::ids_reserved:
        return "ids_reserved";
    case OperationStage::mutated:
        return "mutated";
    case OperationStage::derived_updated:
        return "derived_updated";
    case OperationStage::events_emitted:
        return "events_emitted";
    case OperationStage::replication_marked:
        return "replication_marked";
    case OperationStage::save_marked:
        return "save_marked";
    case OperationStage::committed:
        return "committed";
    case OperationStage::rolled_back:
        return "rolled_back";
    }
    return "unknown";
}

WorldOperation::WorldOperation(std::string name) : name_(std::move(name)) {
    push_stage(OperationStage::begun);
}

const std::string& WorldOperation::name() const noexcept {
    return name_;
}

bool WorldOperation::is_committed() const noexcept {
    return committed_;
}

bool WorldOperation::is_rolled_back() const noexcept {
    return rolled_back_;
}

bool WorldOperation::has_failed() const noexcept {
    return failure_.has_value();
}

const std::vector<OperationStage>& WorldOperation::stages() const noexcept {
    return stages_;
}

const std::vector<OperationEvent>& WorldOperation::events() const noexcept {
    return events_;
}

const std::vector<core::SaveId>& WorldOperation::reserved_ids() const noexcept {
    return reserved_ids_;
}

const std::vector<std::string>& WorldOperation::mutations() const noexcept {
    return mutations_;
}

const std::vector<std::string>& WorldOperation::derived_updates() const noexcept {
    return derived_updates_;
}

bool WorldOperation::replication_dirty() const noexcept {
    return replication_dirty_;
}

bool WorldOperation::save_dirty() const noexcept {
    return save_dirty_;
}

const core::Error& WorldOperation::failure() const {
    if (!failure_.has_value()) {
        throw std::logic_error("WorldOperation::failure() called without a failure");
    }
    return *failure_;
}

core::Status WorldOperation::validate(bool condition, std::string message) {
    if (is_closed()) {
        return fail("operation.closed", "cannot validate a closed world operation");
    }
    if (!condition) {
        return fail("operation.validation_failed", std::move(message));
    }

    push_stage(OperationStage::validated);
    return core::Status::ok();
}

core::Result<core::SaveId> WorldOperation::reserve_save_id(save::SaveIdAllocator& allocator) {
    if (is_closed()) {
        const auto status =
            fail("operation.closed", "cannot reserve ids for a closed world operation");
        return core::Result<core::SaveId>::failure(status.error().code, status.error().message);
    }

    auto id = allocator.reserve();
    if (!id) {
        const auto status = fail(id.error().code, id.error().message);
        return core::Result<core::SaveId>::failure(status.error().code, status.error().message);
    }

    reserved_ids_.push_back(id.value());
    push_stage(OperationStage::ids_reserved);
    return id;
}

core::Status WorldOperation::record_mutation(std::string description) {
    if (is_closed()) {
        return fail("operation.closed", "cannot mutate a closed world operation");
    }
    if (description.empty()) {
        return fail("operation.empty_mutation", "mutation description is required");
    }

    mutations_.push_back(std::move(description));
    push_stage(OperationStage::mutated);
    return core::Status::ok();
}

void WorldOperation::record_derived_update(std::string system_name) {
    if (is_closed() || system_name.empty()) {
        return;
    }

    derived_updates_.push_back(std::move(system_name));
    push_stage(OperationStage::derived_updated);
}

void WorldOperation::emit_event(OperationEvent event) {
    if (is_closed()) {
        return;
    }

    events_.push_back(std::move(event));
    push_stage(OperationStage::events_emitted);
}

void WorldOperation::mark_replication_dirty() {
    if (is_closed()) {
        return;
    }

    replication_dirty_ = true;
    push_stage(OperationStage::replication_marked);
}

void WorldOperation::mark_save_dirty() {
    if (is_closed()) {
        return;
    }

    save_dirty_ = true;
    push_stage(OperationStage::save_marked);
}

core::Status WorldOperation::commit() {
    if (is_closed()) {
        return fail("operation.closed", "world operation is already closed");
    }
    if (failure_.has_value()) {
        rolled_back_ = true;
        push_stage(OperationStage::rolled_back);
        return core::Status::failure(failure_->code, failure_->message);
    }
    if (mutations_.empty()) {
        return fail("operation.no_mutation", "world operation cannot commit without a mutation");
    }
    if (!save_dirty_) {
        return fail("operation.save_not_dirty", "world operation must mark save data dirty");
    }
    if (!replication_dirty_) {
        return fail("operation.replication_not_dirty",
                    "world operation must mark replication state dirty");
    }
    if (events_.empty()) {
        return fail("operation.no_event", "world operation cannot commit without an event");
    }

    committed_ = true;
    push_stage(OperationStage::committed);
    return core::Status::ok();
}

void WorldOperation::rollback(std::string reason) {
    if (is_closed()) {
        return;
    }

    failure_ = core::Error{"operation.rolled_back", std::move(reason)};
    rolled_back_ = true;
    push_stage(OperationStage::rolled_back);
}

void WorldOperation::push_stage(OperationStage stage) {
    stages_.push_back(stage);
}

bool WorldOperation::is_closed() const noexcept {
    return committed_ || rolled_back_;
}

core::Status WorldOperation::fail(std::string code, std::string message) {
    if (!failure_.has_value()) {
        failure_ = core::Error{std::move(code), std::move(message)};
    }
    return core::Status::failure(failure_->code, failure_->message);
}

} // namespace heartstead::world
