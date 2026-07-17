#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/world/coords/world_coords.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace heartstead::simulation {

template <typename Event> class EventStream {
  public:
    [[nodiscard]] core::Status append(Event event) {
        if (!writable_) {
            return core::Status::failure("tick_events.stream_sealed",
                                         "cannot append to a sealed tick event stream");
        }
        events_.push_back(std::move(event));
        return core::Status::ok();
    }

    [[nodiscard]] std::span<const Event> events() const noexcept {
        return events_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return events_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return events_.empty();
    }

  private:
    friend class TickEvents;

    void begin_tick() {
        events_.clear();
        writable_ = true;
    }

    void seal() noexcept {
        writable_ = false;
    }

    std::vector<Event> events_;
    bool writable_ = false;
};

struct EntitySpawned {
    core::RuntimeHandle entity;
    core::PrototypeId prototype;
};

struct EntityDestroyed {
    core::RuntimeHandle entity;
};

struct VoxelChanged {
    world::BlockCoord position;
    world::VoxelCell previous;
    world::VoxelCell current;
};

struct InventoryChanged {
    core::SaveId inventory;
    std::uint64_t revision = 0;
};

class TickEvents {
  public:
    [[nodiscard]] core::Status begin_tick(std::uint64_t tick) {
        if (active_) {
            return core::Status::failure("tick_events.already_active",
                                         "the previous tick event streams are still active");
        }
        tick_ = tick;
        active_ = true;
        sealed_ = false;
        entity_spawned.begin_tick();
        entity_destroyed.begin_tick();
        voxel_changed.begin_tick();
        inventory_changed.begin_tick();
        return core::Status::ok();
    }

    [[nodiscard]] core::Status seal() {
        if (!active_ || sealed_) {
            return core::Status::failure("tick_events.not_writable",
                                         "tick event streams are not active and writable");
        }
        entity_spawned.seal();
        entity_destroyed.seal();
        voxel_changed.seal();
        inventory_changed.seal();
        sealed_ = true;
        return core::Status::ok();
    }

    void clear() noexcept {
        entity_spawned.events_.clear();
        entity_destroyed.events_.clear();
        voxel_changed.events_.clear();
        inventory_changed.events_.clear();
        entity_spawned.seal();
        entity_destroyed.seal();
        voxel_changed.seal();
        inventory_changed.seal();
        active_ = false;
        sealed_ = false;
    }

    [[nodiscard]] std::uint64_t tick() const noexcept {
        return tick_;
    }

    [[nodiscard]] bool is_active() const noexcept {
        return active_;
    }

    [[nodiscard]] bool is_sealed() const noexcept {
        return sealed_;
    }

    [[nodiscard]] std::size_t event_count() const noexcept {
        return entity_spawned.size() + entity_destroyed.size() + voxel_changed.size() +
               inventory_changed.size();
    }

    EventStream<EntitySpawned> entity_spawned;
    EventStream<EntityDestroyed> entity_destroyed;
    EventStream<VoxelChanged> voxel_changed;
    EventStream<InventoryChanged> inventory_changed;

  private:
    std::uint64_t tick_ = 0;
    bool active_ = false;
    bool sealed_ = false;
};

} // namespace heartstead::simulation
