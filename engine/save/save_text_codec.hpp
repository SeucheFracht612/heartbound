#pragma once

#include "engine/core/result.hpp"
#include "engine/save/save_metadata.hpp"
#include "engine/save/save_snapshot.hpp"

#include <string>
#include <string_view>

namespace heartstead::save {

class SaveTextCodec {
  public:
    [[nodiscard]] static std::string encode_metadata(const SaveMetadata& metadata);
    [[nodiscard]] static core::Result<SaveMetadata> decode_metadata(std::string_view text);
    [[nodiscard]] static std::string encode_snapshot(const SaveSnapshot& snapshot);
    [[nodiscard]] static core::Result<SaveSnapshot> decode_snapshot(std::string_view text);
};

} // namespace heartstead::save
