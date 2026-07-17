#include "engine/entities/entity_world.hpp"

#include "engine/simulation/tick_events.hpp"

#include <limits>

namespace heartstead::entities {

core::Result<EntityId> EntityWorld::allocate_id() {
    if (!free_slots_.empty()) {
        const auto index = free_slots_.back();
        free_slots_.pop_back();
        auto& candidate = slots_[index];
        if (candidate.generation == 0) {
            return core::Result<EntityId>::failure("entity_world.generation_exhausted",
                                                   "entity slot generation has wrapped");
        }
        return core::Result<EntityId>::success(EntityId::from_parts(index, candidate.generation));
    }
    if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<EntityId>::failure("entity_world.capacity_exhausted",
                                               "entity slot index range is exhausted");
    }
    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back({});
    return core::Result<EntityId>::success(EntityId::from_parts(index, 1));
}

core::Result<EntityId> EntityWorld::create_entity(core::PrototypeId prototype) {
    if (!prototype.is_valid()) {
        return core::Result<EntityId>::failure("entity_world.invalid_prototype",
                                               "entity prototype id must be valid");
    }
    auto allocated = allocate_id();
    if (!allocated) {
        return allocated;
    }
    auto& target = slots_[allocated.value().index()];
    target.occupied = true;
    target.record = {allocated.value(), std::move(prototype), EntityLifecycle::created,
                     next_revision_++};
    ++live_count_;
    return allocated;
}

core::Status EntityWorld::activate_entity(EntityId id, simulation::TickEvents* events) {
    auto* record = find(id);
    if (record == nullptr) {
        return core::Status::failure("entity_world.entity_not_found", "entity was not found");
    }
    if (record->lifecycle != EntityLifecycle::created &&
        record->lifecycle != EntityLifecycle::sleeping) {
        return core::Status::failure("entity_world.invalid_activation",
                                     "only created or sleeping entities can be activated");
    }
    const auto is_initial_activation = record->lifecycle == EntityLifecycle::created;
    if (events != nullptr && is_initial_activation) {
        auto status = events->entity_spawned.append({id, record->prototype});
        if (!status) {
            return status;
        }
    }
    record->lifecycle = EntityLifecycle::active;
    record->revision = next_revision_++;
    return core::Status::ok();
}

core::Status EntityWorld::deactivate_entity(EntityId id) {
    auto* record = find(id);
    if (record == nullptr) {
        return core::Status::failure("entity_world.entity_not_found", "entity was not found");
    }
    if (record->lifecycle != EntityLifecycle::active) {
        return core::Status::failure("entity_world.invalid_deactivation",
                                     "only active entities can enter sleeping state");
    }
    record->lifecycle = EntityLifecycle::sleeping;
    record->revision = next_revision_++;
    return core::Status::ok();
}

core::Status EntityWorld::destroy_entity(EntityId id) {
    auto* record = find(id);
    if (record == nullptr) {
        return core::Status::failure("entity_world.entity_not_found", "entity was not found");
    }
    if (record->lifecycle == EntityLifecycle::pending_destruction) {
        return core::Status::ok();
    }
    record->lifecycle = EntityLifecycle::pending_destruction;
    record->revision = next_revision_++;
    return core::Status::ok();
}

core::Result<std::size_t>
EntityWorld::finalize_destruction(std::uint64_t tick, simulation::TickEvents* events) {
    std::size_t destroyed = 0;
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        auto& candidate = slots_[index];
        if (!candidate.occupied ||
            candidate.record.lifecycle != EntityLifecycle::pending_destruction) {
            continue;
        }
        const auto id = candidate.record.id;
        for (const auto& cleanup : cleanup_callbacks_) {
            auto status = cleanup.callback(id, candidate.record);
            if (!status) {
                return core::Result<std::size_t>::failure(
                    "entity_world.cleanup_failed", cleanup.name + ": " + status.error().message);
            }
        }
        if (events != nullptr) {
            auto status = events->entity_destroyed.append({id});
            if (!status) {
                return core::Result<std::size_t>::failure(status.error().code,
                                                          status.error().message);
            }
        }
        for (auto& [_, store] : component_stores_) {
            store->erase(id);
        }
        tombstones_.push_back({id, candidate.record.revision, tick});
        candidate.occupied = false;
        candidate.record = {};
        ++candidate.generation;
        if (candidate.generation != 0) {
            free_slots_.push_back(index);
        }
        --live_count_;
        ++destroyed;
    }
    return core::Result<std::size_t>::success(destroyed);
}

bool EntityWorld::is_alive(EntityId id) const noexcept {
    return slot(id) != nullptr;
}

EntityWorldRecord* EntityWorld::find(EntityId id) noexcept {
    auto* found = slot(id);
    return found == nullptr ? nullptr : &found->record;
}

const EntityWorldRecord* EntityWorld::find(EntityId id) const noexcept {
    const auto* found = slot(id);
    return found == nullptr ? nullptr : &found->record;
}

std::vector<EntityWorldRecord> EntityWorld::records() const {
    std::vector<EntityWorldRecord> result;
    result.reserve(live_count_);
    for (const auto& candidate : slots_) {
        if (candidate.occupied) {
            result.push_back(candidate.record);
        }
    }
    return result;
}

core::Status EntityWorld::register_cleanup(std::string name, EntityCleanupCallback callback) {
    if (name.empty() || !callback) {
        return core::Status::failure("entity_world.invalid_cleanup",
                                     "entity cleanup needs a name and callback");
    }
    if (std::ranges::any_of(cleanup_callbacks_, [&name](const CleanupRegistration& registered) {
            return registered.name == name;
        })) {
        return core::Status::failure("entity_world.duplicate_cleanup",
                                     "entity cleanup names must be unique: " + name);
    }
    cleanup_callbacks_.push_back({std::move(name), std::move(callback)});
    return core::Status::ok();
}

std::span<const EntityTombstone> EntityWorld::tombstones() const noexcept {
    return tombstones_;
}

void EntityWorld::clear_tombstones() noexcept {
    tombstones_.clear();
}

EntityWorldStats EntityWorld::stats() const noexcept {
    EntityWorldStats result;
    result.live_entities = live_count_;
    result.component_type_count = component_stores_.size();
    result.tombstone_count = tombstones_.size();
    for (const auto& candidate : slots_) {
        if (!candidate.occupied) {
            continue;
        }
        switch (candidate.record.lifecycle) {
        case EntityLifecycle::active:
            ++result.active_entities;
            break;
        case EntityLifecycle::sleeping:
            ++result.sleeping_entities;
            break;
        case EntityLifecycle::pending_destruction:
            ++result.pending_destruction;
            break;
        case EntityLifecycle::created:
            break;
        }
    }
    for (const auto& [_, store] : component_stores_) {
        result.component_count += store->size();
    }
    return result;
}

void EntityWorld::clear() noexcept {
    for (auto& [_, store] : component_stores_) {
        store->clear();
    }
    slots_.clear();
    free_slots_.clear();
    tombstones_.clear();
    live_count_ = 0;
    next_revision_ = 1;
}

EntityWorld::Slot* EntityWorld::slot(EntityId id) noexcept {
    if (!id.is_valid() || id.index() >= slots_.size()) {
        return nullptr;
    }
    auto& candidate = slots_[id.index()];
    return candidate.occupied && candidate.generation == id.generation() ? &candidate : nullptr;
}

const EntityWorld::Slot* EntityWorld::slot(EntityId id) const noexcept {
    if (!id.is_valid() || id.index() >= slots_.size()) {
        return nullptr;
    }
    const auto& candidate = slots_[id.index()];
    return candidate.occupied && candidate.generation == id.generation() ? &candidate : nullptr;
}

std::string_view entity_lifecycle_name(EntityLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case EntityLifecycle::created:
        return "created";
    case EntityLifecycle::active:
        return "active";
    case EntityLifecycle::sleeping:
        return "sleeping";
    case EntityLifecycle::pending_destruction:
        return "pending_destruction";
    }
    return "unknown";
}

} // namespace heartstead::entities
