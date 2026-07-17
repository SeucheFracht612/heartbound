#pragma once

#include "engine/core/result.hpp"
#include "game/presentation/presentation_world.hpp"

#include <cstdint>
#include <unordered_set>

namespace heartstead::game {

class ClientRuntime;

struct PresentationSynchronizationStats {
    std::uint32_t inserted_objects = 0;
    std::uint32_t updated_objects = 0;
    std::uint32_t removed_objects = 0;
    std::uint32_t unchanged_objects = 0;
};

class ClientPresentationSynchronizer final {
  public:
    [[nodiscard]] core::Result<PresentationSynchronizationStats>
    synchronize(const ClientRuntime& client, PresentationWorld& presentation);
    void clear() noexcept;

  private:
    std::unordered_set<std::uint64_t> retained_players_;
};

} // namespace heartstead::game
