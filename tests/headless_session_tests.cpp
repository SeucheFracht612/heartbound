#include "game/testing/headless_session.hpp"

#include <cassert>
#include <filesystem>

using namespace heartstead;

namespace {

std::filesystem::path source_root() {
    return std::filesystem::path(HEARTSTEAD_TEST_SOURCE_DIR);
}

void test_local_headless_session_advances_shared_runtime() {
    game::HeadlessSessionDesc desc;
    desc.source_root = source_root();
    auto harness = game::HeadlessSessionHarness::create(std::move(desc));
    assert(harness);
    auto report = harness.value()->run_ticks(5);
    assert(report);
    assert(report.value().requested_tick_count == 5);
    assert(report.value().completed_tick_count == 5);
    assert(report.value().last_frame.authoritative_world_tick == 5);
    assert(harness.value()->runtime().session()->client() != nullptr);
    auto snapshot = harness.value()->runtime().capture_render_snapshot();
    assert(snapshot && snapshot.value().objects.size() == 1);
    assert(harness.value()->shutdown());
}

void test_dedicated_headless_session_uses_same_harness_without_client_services() {
    game::HeadlessSessionDesc desc;
    desc.source_root = source_root();
    desc.runtime.create_client = false;
    auto harness = game::HeadlessSessionHarness::create(std::move(desc));
    assert(harness);
    auto report = harness.value()->run_ticks(3);
    assert(report && report.value().completed_tick_count == 3);
    assert(harness.value()->runtime().session()->server() != nullptr);
    assert(harness.value()->runtime().session()->client() == nullptr);
    assert(!harness.value()->runtime().capture_render_snapshot());
    assert(harness.value()->shutdown());
}

} // namespace

int main() {
    test_local_headless_session_advances_shared_runtime();
    test_dedicated_headless_session_uses_same_harness_without_client_services();
    return 0;
}
