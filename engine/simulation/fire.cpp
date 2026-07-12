#include "engine/simulation/fire.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace heartstead::simulation {

core::Status FireDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("fire_definition.invalid_prototype",
                                     "fire definition prototype id is invalid");
    }
    if (ember_window_ticks == 0 || maximum_fuel_buffer_ticks == 0) {
        return core::Status::failure("fire_definition.invalid_duration",
                                     "fire fuel and ember durations must be non-zero");
    }
    if (!std::isfinite(warmth_radius) || warmth_radius < 0.0F || !std::isfinite(repel_radius) ||
        repel_radius < 0.0F) {
        return core::Status::failure("fire_definition.invalid_radius",
                                     "fire warmth and repel radii must be finite and non-negative");
    }
    return core::Status::ok();
}

core::Status FireInstance::validate_record() const {
    if (!fire_id.is_valid() || !prototype_id.is_valid()) {
        return core::Status::failure("fire.invalid_identity",
                                     "fire instance identity or prototype is invalid");
    }
    if (!is_known_fire_state(state)) {
        return core::Status::failure("fire.invalid_state", "fire instance state is unknown");
    }
    if (state == FireState::lit && fuel_buffer_ticks == 0) {
        return core::Status::failure("fire.invalid_lit_state", "lit fire requires fuel");
    }
    if (state == FireState::embers && embers_until <= last_eval) {
        return core::Status::failure("fire.invalid_embers",
                                     "ember deadline must follow evaluation time");
    }
    return core::Status::ok();
}

core::Status FireInstance::validate(const FireDefinition& definition) const {
    auto status = validate_record();
    if (!status)
        return status;
    status = definition.validate();
    if (!status)
        return status;
    if (prototype_id != definition.prototype_id) {
        return core::Status::failure("fire.invalid_identity",
                                     "fire instance prototype does not match definition");
    }
    if (fuel_buffer_ticks > definition.maximum_fuel_buffer_ticks) {
        return core::Status::failure("fire.invalid_fuel", "fire fuel buffer exceeds its maximum");
    }
    return core::Status::ok();
}

bool FireInstance::emits_light() const noexcept {
    return state == FireState::lit || state == FireState::embers;
}

core::Result<FireInstance> FireRuntime::create(core::SaveId fire_id,
                                               const FireDefinition& definition,
                                               WorldTick world_time, bool weather_exposed) {
    auto status = definition.validate();
    if (!status)
        return core::Result<FireInstance>::failure(status.error().code, status.error().message);
    FireInstance fire{fire_id, definition.prototype_id, FireState::unlit, 0, world_time,
                      0,       weather_exposed};
    status = fire.validate(definition);
    if (!status)
        return core::Result<FireInstance>::failure(status.error().code, status.error().message);
    return core::Result<FireInstance>::success(fire);
}

core::Status FireRuntime::feed(FireInstance& fire, const FireDefinition& definition,
                               WorldTick fuel_ticks) {
    if (fuel_ticks == 0)
        return core::Status::failure("fire.invalid_feed", "feeding fire requires non-zero fuel");
    auto status = fire.validate(definition);
    if (!status)
        return status;
    auto staged = fire;
    const auto room = definition.maximum_fuel_buffer_ticks -
                      std::min(definition.maximum_fuel_buffer_ticks, staged.fuel_buffer_ticks);
    if (room == 0)
        return core::Status::failure("fire.fuel_buffer_full", "fire fuel buffer is already full");
    staged.fuel_buffer_ticks += std::min(room, fuel_ticks);
    // Feeding embers does not retroactively relight them from last_eval.  Call ignite with the
    // authoritative world time after feeding so the new burn interval has an exact start.
    status = staged.validate(definition);
    if (status)
        fire = staged;
    return status;
}

core::Status FireRuntime::ignite(FireInstance& fire, const FireDefinition& definition,
                                 WorldTick world_time) {
    auto status = fire.validate(definition);
    if (!status)
        return status;
    if (world_time < fire.last_eval)
        return core::Status::failure("fire.time_reversed", "fire time cannot move backward");
    if (fire.fuel_buffer_ticks == 0)
        return core::Status::failure("fire.no_fuel", "fire cannot ignite without fuel");
    auto staged = fire;
    staged.state = FireState::lit;
    staged.last_eval = world_time;
    staged.embers_until = 0;
    status = staged.validate(definition);
    if (status)
        fire = staged;
    return status;
}

core::Status FireRuntime::evaluate(FireInstance& fire, const FireDefinition& definition,
                                   WorldTick world_time, FireWeatherWindow weather) {
    auto status = fire.validate(definition);
    if (!status)
        return status;
    if (world_time < fire.last_eval)
        return core::Status::failure("fire.time_reversed", "fire time cannot move backward");
    if (weather.first_extinguishing_tick.has_value() &&
        (*weather.first_extinguishing_tick < fire.last_eval ||
         *weather.first_extinguishing_tick > world_time)) {
        return core::Status::failure("fire.invalid_weather_window",
                                     "weather event must lie inside elapsed window");
    }
    auto staged = fire;
    if (staged.state == FireState::lit) {
        auto burn_until = world_time;
        if (staged.weather_exposed && weather.first_extinguishing_tick.has_value()) {
            burn_until = std::min(burn_until, *weather.first_extinguishing_tick);
        }
        const auto elapsed = burn_until - staged.last_eval;
        if (elapsed >= staged.fuel_buffer_ticks) {
            const auto exhausted_at = staged.last_eval + staged.fuel_buffer_ticks;
            staged.fuel_buffer_ticks = 0;
            staged.embers_until =
                exhausted_at > std::numeric_limits<WorldTick>::max() - definition.ember_window_ticks
                    ? std::numeric_limits<WorldTick>::max()
                    : exhausted_at + definition.ember_window_ticks;
            staged.state = world_time < staged.embers_until ? FireState::embers : FireState::out;
        } else {
            staged.fuel_buffer_ticks -= elapsed;
            if (staged.weather_exposed && weather.first_extinguishing_tick.has_value()) {
                staged.embers_until =
                    *weather.first_extinguishing_tick +
                    std::min(definition.ember_window_ticks, std::numeric_limits<WorldTick>::max() -
                                                                *weather.first_extinguishing_tick);
                staged.state =
                    world_time < staged.embers_until ? FireState::embers : FireState::out;
            }
        }
    } else if (staged.state == FireState::embers && world_time >= staged.embers_until) {
        staged.state = FireState::out;
    }
    staged.last_eval = world_time;
    if (staged.state == FireState::out)
        staged.embers_until = 0;
    status = staged.validate(definition);
    if (status)
        fire = staged;
    return status;
}

bool is_known_fire_state(FireState state) noexcept {
    switch (state) {
    case FireState::unlit:
    case FireState::lit:
    case FireState::embers:
    case FireState::out:
        return true;
    }
    return false;
}

} // namespace heartstead::simulation
