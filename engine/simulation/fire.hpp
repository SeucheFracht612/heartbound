#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/simulation/world_time.hpp"

#include <cstdint>
#include <optional>

namespace heartstead::simulation {

enum class FireState {
    unlit,
    lit,
    embers,
    out,
};

struct FireDefinition {
    core::PrototypeId prototype_id;
    WorldTick ember_window_ticks = 0;
    WorldTick maximum_fuel_buffer_ticks = 0;
    std::uint8_t light_level = 0;
    float warmth_radius = 0.0F;
    float repel_radius = 0.0F;
    std::uint8_t cook_slot_count = 0;

    [[nodiscard]] core::Status validate() const;
};

struct FireWeatherWindow {
    std::optional<WorldTick> first_extinguishing_tick;
};

struct FireInstance {
    core::SaveId fire_id;
    core::PrototypeId prototype_id;
    FireState state = FireState::unlit;
    WorldTick fuel_buffer_ticks = 0;
    WorldTick last_eval = 0;
    WorldTick embers_until = 0;
    bool weather_exposed = false;

    [[nodiscard]] core::Status validate_record() const;
    [[nodiscard]] core::Status validate(const FireDefinition& definition) const;
    [[nodiscard]] bool emits_light() const noexcept;
};

class FireRuntime {
  public:
    [[nodiscard]] static core::Result<FireInstance> create(core::SaveId fire_id,
                                                           const FireDefinition& definition,
                                                           WorldTick world_time,
                                                           bool weather_exposed);
    [[nodiscard]] static core::Status feed(FireInstance& fire, const FireDefinition& definition,
                                           WorldTick fuel_ticks);
    [[nodiscard]] static core::Status ignite(FireInstance& fire, const FireDefinition& definition,
                                             WorldTick world_time);
    [[nodiscard]] static core::Status evaluate(FireInstance& fire, const FireDefinition& definition,
                                               WorldTick world_time,
                                               FireWeatherWindow weather = {});
};

[[nodiscard]] bool is_known_fire_state(FireState state) noexcept;

} // namespace heartstead::simulation
