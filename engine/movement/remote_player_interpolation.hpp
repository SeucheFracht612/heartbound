#pragma once

#include "engine/core/result.hpp"
#include "engine/movement/movement_prediction.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace heartstead::movement {

class RemotePlayerInterpolator {
  public:
    explicit RemotePlayerInterpolator(std::uint32_t delay_ticks = 6,
                                      std::size_t capacity = 32);

    [[nodiscard]] core::Status push(PlayerControllerSnapshot snapshot,
                                    const PlayerMovementConfig& config = {});
    [[nodiscard]] core::Result<PlayerControllerState> sample(std::uint64_t render_tick) const;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint32_t delay_ticks() const noexcept;

  private:
    std::uint32_t delay_ticks_ = 6;
    std::size_t capacity_ = 32;
    std::deque<PlayerControllerSnapshot> snapshots_;
};

} // namespace heartstead::movement
