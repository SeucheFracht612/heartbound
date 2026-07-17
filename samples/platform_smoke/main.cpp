#include "engine/core/ids.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/platform/platform.hpp"

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        core::log(core::LogLevel::info, "Heartstead platform smoke starting");
        const auto native_info = platform::platform_backend_info(platform::PlatformBackend::native);
        core::log(core::LogLevel::info,
                  "Native platform backend status: " + std::string(native_info.status));
        const auto native_capabilities =
            platform::platform_backend_capabilities(platform::PlatformBackend::native);
        core::log(
            core::LogLevel::info,
            "Native platform contract: windows=" +
                std::string(native_capabilities.supports_native_windows ? "native" : "logical") +
                ", text_input=" +
                std::string(native_capabilities.supports_text_input ? "yes" : "no") +
                ", mouse_input=" +
                std::string(native_capabilities.supports_mouse_input ? "yes" : "no") +
                ", displays=" +
                std::string(native_capabilities.supports_display_metadata ? "yes" : "no") +
                ", vulkan_surface=" +
                std::string(native_capabilities.supports_vulkan_surface ? "yes" : "no") +
                ", clipboard=" +
                std::string(native_capabilities.supports_clipboard ? "yes" : "no") +
                ", window_system=" + std::string(native_capabilities.window_system));

        const auto save_id = core::SaveId::from_value(1);
        core::log(core::LogLevel::info, "Allocated sample SaveId " + save_id.to_string());

        auto platform = platform::create_platform(platform::PlatformDesc{
            native_info.available ? platform::PlatformBackend::native
                                  : platform::PlatformBackend::headless,
        });
        if (!platform) {
            core::log(core::LogLevel::error, platform.error().message);
            return 1;
        }
        core::log(core::LogLevel::info,
                  "Selected platform backend: " + std::string(platform.value()->backend_name()));
        if (platform.value()->capabilities().supports_clipboard) {
            auto clipboard_set = platform.value()->set_clipboard_text("heartstead clipboard smoke");
            if (!clipboard_set) {
                core::log(core::LogLevel::error, clipboard_set.error().message);
                return 1;
            }
            auto clipboard_text = platform.value()->clipboard_text();
            if (!clipboard_text) {
                core::log(core::LogLevel::error, clipboard_text.error().message);
                return 1;
            }
            core::log(core::LogLevel::info, "Clipboard text: " + clipboard_text.value());
        }
        const auto displays = platform.value()->displays();
        core::log(core::LogLevel::info, "Display count: " + std::to_string(displays.size()));
        for (const auto& display : displays) {
            auto message = "Display " + std::to_string(display.index) + " " + display.name + " " +
                           std::to_string(display.width_px) + "x" +
                           std::to_string(display.height_px) + " at " +
                           std::to_string(display.x_px) + "," + std::to_string(display.y_px);
            if (display.width_mm > 0 && display.height_mm > 0) {
                message += " " + std::to_string(display.width_mm) + "x" +
                           std::to_string(display.height_mm) + "mm";
            }
            if (display.refresh_hz > 0.0) {
                message += " " + std::to_string(display.refresh_hz) + "Hz";
            }
            if (display.primary) {
                message += " primary";
            }
            core::log(core::LogLevel::info, message);
        }

        auto window = platform.value()->create_window(
            platform::WindowDesc{"Heartstead Platform Smoke", 800, 450, true});
        if (!window) {
            core::log(core::LogLevel::error, window.error().message);
            return 1;
        }

        auto queued = platform.value()->queue_event(
            platform::PlatformEvent{platform::PlatformEventKind::window_resized,
                                    window.value(),
                                    platform::KeyCode::unknown,
                                    1024,
                                    576,
                                    {}});
        if (!queued) {
            core::log(core::LogLevel::error, queued.error().message);
            return 1;
        }
        queued = platform.value()->queue_event(
            platform::PlatformEvent{platform::PlatformEventKind::text_input, window.value(),
                                    platform::KeyCode::unknown, 0, 0, "smoke"});
        if (!queued) {
            core::log(core::LogLevel::error, queued.error().message);
            return 1;
        }
        queued = platform.value()->queue_event(
            platform::PlatformEvent{platform::PlatformEventKind::key_down,
                                    window.value(),
                                    platform::KeyCode::escape,
                                    0,
                                    0,
                                    {}});
        if (!queued) {
            core::log(core::LogLevel::error, queued.error().message);
            return 1;
        }
        queued = platform.value()->queue_event(
            platform::PlatformEvent{platform::PlatformEventKind::mouse_moved,
                                    window.value(),
                                    platform::KeyCode::unknown,
                                    0,
                                    0,
                                    {},
                                    platform::MouseButton::unknown,
                                    240,
                                    120,
                                    0,
                                    0});
        if (!queued) {
            core::log(core::LogLevel::error, queued.error().message);
            return 1;
        }
        queued = platform.value()->queue_event(
            platform::PlatformEvent{platform::PlatformEventKind::mouse_button_down,
                                    window.value(),
                                    platform::KeyCode::unknown,
                                    0,
                                    0,
                                    {},
                                    platform::MouseButton::left,
                                    240,
                                    120,
                                    0,
                                    0});
        if (!queued) {
            core::log(core::LogLevel::error, queued.error().message);
            return 1;
        }

        const auto window_id = window.value();
        auto run_status = platform::run_platform_app(
            *platform.value(), platform::AppRunConfig{8},
            [window_id](platform::IPlatform& active_platform, std::uint64_t frame,
                        std::int64_t now_ms) {
                while (auto event = active_platform.poll_event()) {
                    core::log(core::LogLevel::info,
                              "Platform event " +
                                  std::string(platform::platform_event_kind_name(event->kind)));
                }
                if (const auto snapshot = active_platform.input_snapshot(window_id);
                    snapshot && !snapshot->text.empty()) {
                    core::log(core::LogLevel::info, "Text input " + snapshot->text.front());
                }
                if (const auto snapshot = active_platform.input_snapshot(window_id);
                    snapshot && active_platform.was_mouse_button_pressed(
                                    window_id, platform::MouseButton::left)) {
                    core::log(
                        core::LogLevel::info,
                        "Mouse " +
                            std::string(platform::mouse_button_name(platform::MouseButton::left)) +
                            " pressed at " + std::to_string(snapshot->mouse.x) + "," +
                            std::to_string(snapshot->mouse.y));
                }
                if (active_platform.was_key_pressed(window_id, platform::KeyCode::escape)) {
                    core::log(core::LogLevel::info, "Escape pressed");
                    active_platform.request_quit();
                }
                core::log(core::LogLevel::debug, "Platform frame " + std::to_string(frame) +
                                                     " at " + std::to_string(now_ms));
                return core::Status::ok();
            });

        if (!run_status) {
            core::log(core::LogLevel::error, run_status.error().message);
            return 1;
        }

        auto close_status = platform.value()->close_window(window_id);
        if (!close_status) {
            core::log(core::LogLevel::error, close_status.error().message);
            return 1;
        }

        core::log(core::LogLevel::info, "Clean shutdown");
        return 0;
    });
}
