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
    const auto room = definition.maximum_fuel_buffer_ticks -
                      std::min(definition.maximum_fuel_buffer_ticks, fire.fuel_buffer_ticks);
    fire.fuel_buffer_ticks += std::min(room, fuel_ticks);
    if (fire.state == FireState::embers)
        fire.state = FireState::lit;
    return fire.validate(definition);
}

core::Status FireRuntime::ignite(FireInstance& fire, const FireDefinition& definition,
                                 WorldTick world_time) {
    if (world_time < fire.last_eval)
        return core::Status::failure("fire.time_reversed", "fire time cannot move backward");
    if (fire.fuel_buffer_ticks == 0)
        return core::Status::failure("fire.no_fuel", "fire cannot ignite without fuel");
    fire.state = FireState::lit;
    fire.last_eval = world_time;
    fire.embers_until = 0;
    return fire.validate(definition);
}

core::Status FireRuntime::evaluate(FireInstance& fire, const FireDefinition& definition,
                                   WorldTick world_time, FireWeatherWindow weather) {
    if (world_time < fire.last_eval)
        return core::Status::failure("fire.time_reversed", "fire time cannot move backward");
    if (weather.first_extinguishing_tick.has_value() &&
        (*weather.first_extinguishing_tick < fire.last_eval ||
         *weather.first_extinguishing_tick > world_time)) {
        return core::Status::failure("fire.invalid_weather_window",
                                     "weather event must lie inside elapsed window");
    }
    if (fire.state == FireState::lit) {
        auto burn_until = world_time;
        if (fire.weather_exposed && weather.first_extinguishing_tick.has_value()) {
            burn_until = std::min(burn_until, *weather.first_extinguishing_tick);
        }
        const auto elapsed = burn_until - fire.last_eval;
        if (elapsed >= fire.fuel_buffer_ticks) {
            const auto exhausted_at = fire.last_eval + fire.fuel_buffer_ticks;
            fire.fuel_buffer_ticks = 0;
            fire.embers_until =
                exhausted_at > std::numeric_limits<WorldTick>::max() - definition.ember_window_ticks
                    ? std::numeric_limits<WorldTick>::max()
                    : exhausted_at + definition.ember_window_ticks;
            fire.state = world_time < fire.embers_until ? FireState::embers : FireState::out;
        } else {
            fire.fuel_buffer_ticks -= elapsed;
            if (fire.weather_exposed && weather.first_extinguishing_tick.has_value()) {
                fire.embers_until =
                    *weather.first_extinguishing_tick +
                    std::min(definition.ember_window_ticks, std::numeric_limits<WorldTick>::max() -
                                                                *weather.first_extinguishing_tick);
                fire.state = world_time < fire.embers_until ? FireState::embers : FireState::out;
            }
        }
    } else if (fire.state == FireState::embers && world_time >= fire.embers_until) {
        fire.state = FireState::out;
    }
    fire.last_eval = world_time;
    if (fire.state == FireState::out)
        fire.embers_until = 0;
    return fire.validate(definition);
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
