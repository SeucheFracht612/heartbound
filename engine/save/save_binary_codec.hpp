#pragma once

#include "engine/core/result.hpp"
#include "engine/save/save_snapshot.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace heartstead::save {

class SaveBinaryCodec {
  public:
    [[nodiscard]] static core::Result<std::vector<std::uint8_t>>
    encode_snapshot(const SaveSnapshot& snapshot);
    [[nodiscard]] static core::Result<SaveSnapshot>
    decode_snapshot(std::span<const std::uint8_t> bytes);
};

} // namespace heartstead::save
