#pragma once

#include "engine/core/result.hpp"
#include "game/presentation/presentation_world.hpp"
#include "game/framework/gameplay_module.hpp"

#include <cstdint>
#include <unordered_set>

namespace heartstead::game {

class ClientRuntime;

struct PresentationSynchronizationStats : PresentationAdapterStats {};

class ClientPresentationSynchronizer final {
  public:
    [[nodiscard]] core::Result<PresentationSynchronizationStats>
    synchronize(const ClientRuntime& client, PresentationWorld& presentation);
    void clear() noexcept;

  private:
    std::unordered_set<std::uint64_t> retained_players_;
};

} // namespace heartstead::game
