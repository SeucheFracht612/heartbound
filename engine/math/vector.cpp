#include "engine/math/vector.hpp"

namespace heartstead::math {

std::string_view axis3_name(Axis3 axis) noexcept {
    switch (axis) {
    case Axis3::x:
        return "x";
    case Axis3::y:
        return "y";
    case Axis3::z:
        return "z";
    }
    return "unknown";
}

} // namespace heartstead::math
