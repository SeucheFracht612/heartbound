#pragma once

#include "engine/core/result.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/movement/player_input.hpp"
#include "engine/physics/physics_world.hpp"
#include "engine/save/save_metadata.hpp"
#include "engine/simulation/fixed_step.hpp"
#include "engine/world/voxels/voxel_palette.hpp"
#include "game/runtime/client_runtime.hpp"
#include "game/runtime/server_runtime.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace heartstead::game {

struct RuntimeConfiguration {
    bool create_server = true;
    bool create_client = true;
    bool create_renderer = false;
    bool create_audio = false;
    bool use_in_memory_transport = true;
    bool headless = true;
    simulation::FixedStepConfig fixed_step{};
    physics::PhysicsBackend physics_backend = physics::PhysicsBackend::headless;

    [[nodiscard]] core::Status validate() const;
};

struct SessionRequest {
    save::SaveMetadata metadata;
    std::string scenario_id = "base:scenarios/homestead";
};

struct RuntimeFrameInput {
    std::uint64_t frame_time_us = 0;
    std::int64_t now_ms = 0;
};

struct RuntimeFrameStats {
    simulation::FixedStepFrame fixed_step;
    std::vector<ServerRuntimeTickStats> server_ticks;
    ClientRuntimeStats client;
    std::uint64_t authoritative_world_tick = 0;
};

class RuntimeSession final {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<RuntimeSession>>
    create(RuntimeConfiguration config, SessionRequest request,
           const modding::PrototypeRegistry& prototypes, const world::VoxelPalette& voxel_palette);

    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;
    ~RuntimeSession();

    [[nodiscard]] core::Result<RuntimeFrameStats> run_frame(RuntimeFrameInput input);
    [[nodiscard]] core::Status submit_command(std::string type, std::string payload,
                                              std::int64_t now_ms = 0);
    [[nodiscard]] core::Status submit_player_input(const movement::PlayerInputFrame& input,
                                                   std::int64_t now_ms = 0);
    [[nodiscard]] core::Status shutdown();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] ServerRuntime* server() noexcept;
    [[nodiscard]] const ServerRuntime* server() const noexcept;
    [[nodiscard]] ClientRuntime* client() noexcept;
    [[nodiscard]] const ClientRuntime* client() const noexcept;
    [[nodiscard]] const RuntimeConfiguration& config() const noexcept;

  private:
    RuntimeSession(RuntimeConfiguration config, SessionRequest request,
                   const modding::PrototypeRegistry& prototypes,
                   const world::VoxelPalette& voxel_palette);
    [[nodiscard]] core::Status initialize();
    [[nodiscard]] core::Status pump_client_messages();

    RuntimeConfiguration config_;
    SessionRequest request_;
    const modding::PrototypeRegistry* prototypes_ = nullptr;
    const world::VoxelPalette* voxel_palette_ = nullptr;
    simulation::FixedStepClock fixed_step_;
    std::unique_ptr<ServerRuntime> server_;
    std::unique_ptr<ClientRuntime> client_;
    bool running_ = false;
};

} // namespace heartstead::game
