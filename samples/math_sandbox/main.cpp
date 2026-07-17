#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/math/vector.hpp"

#include <string>

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        core::log(core::LogLevel::info, "Heartstead math sandbox starting");

        const math::Vec3f forward{0.0F, 0.0F, 1.0F};
        const math::Vec3f up{0.0F, 1.0F, 0.0F};
        const auto right = math::cross(up, forward);

        math::Bounds3f bounds{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
        const auto expanded = bounds.expanded(2.0F);

        math::Transform3d transform;
        transform.position = {2.0, 3.0, 4.0};
        transform.rotation_degrees = {0.0, 90.0, 0.0};

        if (!bounds.contains({0.0F, 0.0F, 0.0F}) || !expanded.contains({2.0F, 0.0F, 0.0F}) ||
            !transform.is_finite()) {
            core::log(core::LogLevel::error, "math sandbox validation failed");
            return 1;
        }

        core::log(core::LogLevel::info, "Right vector: " + std::to_string(right.x) + ", " +
                                            std::to_string(right.y) + ", " +
                                            std::to_string(right.z));
        core::log(core::LogLevel::info, "Forward length: " + std::to_string(math::length(forward)));
        return 0;
    });
}
