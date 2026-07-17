#include "engine/platform/platform.hpp"

#include <cassert>

namespace {

using namespace heartstead;

[[nodiscard]] platform::PlatformEvent key_event(platform::PlatformEventKind kind,
                                                platform::WindowId window, platform::KeyCode key) {
    platform::PlatformEvent event;
    event.kind = kind;
    event.window_id = window;
    event.key = key;
    return event;
}

[[nodiscard]] platform::PlatformEvent mouse_event(platform::PlatformEventKind kind,
                                                  platform::WindowId window,
                                                  platform::MouseButton button) {
    platform::PlatformEvent event;
    event.kind = kind;
    event.window_id = window;
    event.mouse_button = button;
    return event;
}

void drain_events(platform::HeadlessPlatform& platform) {
    while (platform.poll_event()) {
    }
}

void test_edges_accumulate_within_a_frame() {
    platform::HeadlessPlatform platform;
    auto window = platform.create_window({});
    assert(window);
    drain_events(platform);

    platform.begin_frame();
    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_down, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_down, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_down,
                                            window.value(), platform::MouseButton::left)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_down,
                                            window.value(), platform::MouseButton::left)));
    drain_events(platform);
    assert(platform.is_key_down(window.value(), platform::KeyCode::w));
    assert(platform.was_key_pressed(window.value(), platform::KeyCode::w));
    assert(platform.is_mouse_button_down(window.value(), platform::MouseButton::left));
    assert(platform.was_mouse_button_pressed(window.value(), platform::MouseButton::left));

    platform.begin_frame();
    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_up, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_up, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_up,
                                            window.value(), platform::MouseButton::left)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_up,
                                            window.value(), platform::MouseButton::left)));
    drain_events(platform);
    assert(!platform.is_key_down(window.value(), platform::KeyCode::w));
    assert(platform.was_key_released(window.value(), platform::KeyCode::w));
    assert(!platform.is_mouse_button_down(window.value(), platform::MouseButton::left));
    assert(platform.was_mouse_button_released(window.value(), platform::MouseButton::left));

    platform.begin_frame();
    assert(!platform.was_key_pressed(window.value(), platform::KeyCode::w));
    assert(!platform.was_key_released(window.value(), platform::KeyCode::w));
    assert(!platform.was_mouse_button_pressed(window.value(), platform::MouseButton::left));
    assert(!platform.was_mouse_button_released(window.value(), platform::MouseButton::left));

    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_down, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_down,
                                            window.value(), platform::MouseButton::left)));
    drain_events(platform);
    platform.begin_frame();

    assert(platform.queue_event(
        key_event(platform::PlatformEventKind::key_up, window.value(), platform::KeyCode::w)));
    assert(platform.queue_event(mouse_event(platform::PlatformEventKind::mouse_button_up,
                                            window.value(), platform::MouseButton::left)));
    platform::PlatformEvent focus_lost;
    focus_lost.kind = platform::PlatformEventKind::window_focus_lost;
    focus_lost.window_id = window.value();
    assert(platform.queue_event(focus_lost));
    drain_events(platform);
    assert(platform.was_key_released(window.value(), platform::KeyCode::w));
    assert(platform.was_mouse_button_released(window.value(), platform::MouseButton::left));

    const auto snapshot = platform.input_snapshot(window.value());
    assert(snapshot);
    assert(snapshot->released_keys.size() == 1);
    assert(snapshot->released_keys.front() == platform::KeyCode::w);
    assert(snapshot->released_mouse_buttons.size() == 1);
    assert(snapshot->released_mouse_buttons.front() == platform::MouseButton::left);
}

} // namespace

int main() {
    test_edges_accumulate_within_a_frame();
    return 0;
}
