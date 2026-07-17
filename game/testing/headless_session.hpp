#pragma once

#include "engine/core/result.hpp"
#include "engine/save/save_snapshot.hpp"
#include "game/runtime/game_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace heartstead::game {

struct HeadlessSessionDesc {
    std::filesystem::path source_root;
    GameRuntimeConfig game;
    RuntimeConfiguration runtime;
    std::string world_name = "headless-test";
    std::uint64_t world_seed = 1;
    std::optional<save::SaveSnapshot> initial_snapshot;
};

struct HeadlessRunReport {
    std::uint32_t requested_tick_count = 0;
    std::uint32_t completed_tick_count = 0;
    std::uint32_t frame_count = 0;
    RuntimeFrameStats last_frame;
};

class HeadlessSessionHarness final {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<HeadlessSessionHarness>>
    create(HeadlessSessionDesc desc);

    HeadlessSessionHarness(const HeadlessSessionHarness&) = delete;
    HeadlessSessionHarness& operator=(const HeadlessSessionHarness&) = delete;
    ~HeadlessSessionHarness();

    [[nodiscard]] core::Result<HeadlessRunReport> run_ticks(std::uint32_t tick_count);
    [[nodiscard]] core::Status shutdown();
    [[nodiscard]] GameRuntime& runtime() noexcept;
    [[nodiscard]] const GameRuntime& runtime() const noexcept;

  private:
    explicit HeadlessSessionHarness(GameRuntime runtime);

    GameRuntime runtime_;
    std::uint64_t elapsed_us_ = 0;
};

} // namespace heartstead::game
