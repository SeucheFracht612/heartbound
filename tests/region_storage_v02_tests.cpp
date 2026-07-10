#include "engine/world/regions/chunk_region_file.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace {

void test_negative_and_extreme_region_mapping() {
    using namespace heartstead::world;
    constexpr auto negative = chunk_region_address({-1, -8, -9});
    static_assert(negative.region == ChunkRegionCoord{-1, -1, -2});
    static_assert(negative.local == ChunkRegionLocalCoord{7, 0, 7});
    auto reconstructed = chunk_coord_from_region(negative.region, negative.local);
    assert(reconstructed);
    assert((reconstructed.value() == ChunkCoord{-1, -8, -9}));

    for (const ChunkCoord coord :
         {ChunkCoord{std::numeric_limits<std::int64_t>::min(), 0, 0},
          ChunkCoord{std::numeric_limits<std::int64_t>::max(), 0, 0}}) {
        const auto address = chunk_region_address(coord);
        auto round_trip = chunk_coord_from_region(address.region, address.local);
        assert(round_trip);
        assert(round_trip.value() == coord);
    }
}

void test_cubic_region_codec_and_checksum() {
    using namespace heartstead::world;
    ChunkRegionFile region;
    region.coord = {-4, 5, -6};
    assert(region.upsert({{0, 0, 0}, 11, 21, "first cubic chunk"}));
    assert(region.upsert({{7, 7, 7}, 12, 22, "opposite cubic chunk"}));
    assert(region.upsert({{0, 0, 0}, 13, 23, "updated cubic chunk"}));
    assert(region.chunks.size() == 2);

    auto encoded = ChunkRegionBinaryCodec::encode(region);
    assert(encoded);
    auto decoded = ChunkRegionBinaryCodec::decode(encoded.value());
    assert(decoded);
    assert(decoded.value().coord == region.coord);
    assert(decoded.value().find({0, 0, 0})->generation_stamp == 13);
    assert(decoded.value().find({7, 7, 7})->encoded_chunk == "opposite cubic chunk");

    auto corrupt = encoded.value();
    corrupt.back() ^= 0x1U;
    auto rejected = ChunkRegionBinaryCodec::decode(corrupt);
    assert(!rejected);
    assert(rejected.error().code == "chunk_region.checksum_mismatch");
}

void test_region_file_store_uses_three_dimensional_name() {
    using namespace heartstead::world;
    const auto root = std::filesystem::temp_directory_path() / "heartstead_region_storage_v02";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    ChunkRegionFile region{{1, -2, 3}, {{{4, 5, 6}, 7, 8, "persisted"}}};
    ChunkRegionFileStore store(root);
    assert(store.path_for(region.coord).filename() == "r.1.-2.3.hsr");
    assert(store.save(region));
    auto loaded = store.load(region.coord);
    assert(loaded);
    assert(loaded.value().find({4, 5, 6})->encoded_chunk == "persisted");

    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main() {
    test_negative_and_extreme_region_mapping();
    test_cubic_region_codec_and_checksum();
    test_region_file_store_uses_three_dimensional_name();
    return 0;
}
