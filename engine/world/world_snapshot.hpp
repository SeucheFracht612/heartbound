#pragma once

#include "engine/core/result.hpp"
#include "engine/save/save_snapshot.hpp"
#include "engine/world/world_state.hpp"

#include <cstdint>

namespace heartstead::world {

struct WorldSnapshotLoadConfig {
    std::uint64_t next_save_id = 1;
    std::uint64_t next_runtime_handle = 1;
    std::uint64_t next_process_id = 1;
    std::uint64_t next_entity_net_id = 1'000'000;
};

class WorldSnapshotBridge {
  public:
    [[nodiscard]] static core::Result<save::SaveSnapshot> export_snapshot(const WorldState& state);
    [[nodiscard]] static core::Result<WorldState>
    import_snapshot(const save::SaveSnapshot& snapshot, WorldSnapshotLoadConfig config = {});
    [[nodiscard]] static core::Result<WorldState>
    import_validated_snapshot(const save::SaveSnapshot& snapshot,
                              const modding::PrototypeRegistry& prototypes,
                              WorldSnapshotLoadConfig config = {});
};

} // namespace heartstead::world
