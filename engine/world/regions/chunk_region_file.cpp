#include "engine/world/regions/chunk_region_file.hpp"

#include "engine/core/hash.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

namespace heartstead::world {

namespace {

constexpr std::array<std::uint8_t, 8> magic{'H', 'S', 'T', 'D', 'R', 'E', 'G', '3'};
constexpr std::uint32_t version = 1;
constexpr std::size_t max_region_file_bytes = 256U * 1024U * 1024U;
constexpr std::uint32_t max_chunk_payload_bytes = 32U * 1024U * 1024U;

class Writer {
  public:
    void u8(std::uint8_t value) {
        bytes_.push_back(value);
    }
    void u32(std::uint32_t value) {
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }
    void u64(std::uint64_t value) {
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }
    void i64(std::int64_t value) {
        u64(static_cast<std::uint64_t>(value));
    }
    void text(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    [[nodiscard]] std::vector<std::uint8_t> take() && {
        return std::move(bytes_);
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] core::Result<std::uint8_t> u8() {
        if (offset_ >= bytes_.size()) {
            return core::Result<std::uint8_t>::failure("chunk_region.truncated",
                                                       "chunk region file is truncated");
        }
        return core::Result<std::uint8_t>::success(bytes_[offset_++]);
    }
    [[nodiscard]] core::Result<std::uint32_t> u32() {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            auto byte = u8();
            if (!byte)
                return core::Result<std::uint32_t>::failure(byte.error().code,
                                                            byte.error().message);
            value |= static_cast<std::uint32_t>(byte.value()) << shift;
        }
        return core::Result<std::uint32_t>::success(value);
    }
    [[nodiscard]] core::Result<std::uint64_t> u64() {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            auto byte = u8();
            if (!byte)
                return core::Result<std::uint64_t>::failure(byte.error().code,
                                                            byte.error().message);
            value |= static_cast<std::uint64_t>(byte.value()) << shift;
        }
        return core::Result<std::uint64_t>::success(value);
    }
    [[nodiscard]] core::Result<std::int64_t> i64() {
        auto value = u64();
        return value
                   ? core::Result<std::int64_t>::success(static_cast<std::int64_t>(value.value()))
                   : core::Result<std::int64_t>::failure(value.error().code, value.error().message);
    }
    [[nodiscard]] core::Result<std::string> text() {
        auto size = u32();
        if (!size)
            return core::Result<std::string>::failure(size.error().code, size.error().message);
        if (size.value() > max_chunk_payload_bytes) {
            return core::Result<std::string>::failure("chunk_region.payload_too_large",
                                                      "chunk region payload exceeds safety limit");
        }
        if (bytes_.size() - offset_ < size.value()) {
            return core::Result<std::string>::failure("chunk_region.truncated",
                                                      "chunk region payload is truncated");
        }
        std::string result(reinterpret_cast<const char*>(bytes_.data() + offset_), size.value());
        offset_ += size.value();
        return core::Result<std::string>::success(std::move(result));
    }
    [[nodiscard]] bool eof() const noexcept {
        return offset_ == bytes_.size();
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] std::uint64_t payload_hash(std::string_view payload) noexcept {
    core::StableHash64 hash;
    hash.add_string(payload);
    return hash.value();
}

} // namespace

core::Result<ChunkCoord> chunk_coord_from_region(ChunkRegionCoord region,
                                                 ChunkRegionLocalCoord local) {
    if (local.x >= chunk_region_edge || local.y >= chunk_region_edge ||
        local.z >= chunk_region_edge) {
        return core::Result<ChunkCoord>::failure("chunk_region.invalid_local",
                                                 "chunk region local coordinate is outside 0..7");
    }
    const auto axis = [](std::int64_t value, std::uint8_t offset) -> core::Result<std::int64_t> {
        constexpr auto edge = static_cast<std::int64_t>(chunk_region_edge);
        constexpr auto min = std::numeric_limits<std::int64_t>::min();
        constexpr auto max = std::numeric_limits<std::int64_t>::max();
        if (value < min / edge || value > (max - offset) / edge) {
            return core::Result<std::int64_t>::failure("chunk_region.coord_overflow",
                                                       "chunk region address overflows int64");
        }
        return core::Result<std::int64_t>::success(value * edge + offset);
    };
    auto x = axis(region.x, local.x);
    auto y = axis(region.y, local.y);
    auto z = axis(region.z, local.z);
    if (!x || !y || !z) {
        const auto& error = !x ? x.error() : (!y ? y.error() : z.error());
        return core::Result<ChunkCoord>::failure(error.code, error.message);
    }
    return core::Result<ChunkCoord>::success({x.value(), y.value(), z.value()});
}

core::Status ChunkRegionFile::validate() const {
    if (chunks.size() > chunk_region_capacity) {
        return core::Status::failure("chunk_region.too_many_chunks",
                                     "cubic region cannot contain more than 512 chunks");
    }
    std::set<ChunkRegionLocalCoord> locals;
    for (const auto& chunk : chunks) {
        if (chunk.local.x >= chunk_region_edge || chunk.local.y >= chunk_region_edge ||
            chunk.local.z >= chunk_region_edge) {
            return core::Status::failure("chunk_region.invalid_local",
                                         "chunk region local coordinate is outside 0..7");
        }
        if (!locals.insert(chunk.local).second) {
            return core::Status::failure("chunk_region.duplicate_chunk",
                                         "chunk region contains duplicate local coordinates");
        }
        if (chunk.encoded_chunk.empty() || chunk.encoded_chunk.size() > max_chunk_payload_bytes) {
            return core::Status::failure("chunk_region.invalid_payload",
                                         "chunk region payload is empty or exceeds safety limit");
        }
    }
    return core::Status::ok();
}

const ChunkRegionRecord* ChunkRegionFile::find(ChunkRegionLocalCoord local) const noexcept {
    for (const auto& chunk : chunks)
        if (chunk.local == local)
            return &chunk;
    return nullptr;
}

core::Status ChunkRegionFile::upsert(ChunkRegionRecord record) {
    for (auto& existing : chunks) {
        if (existing.local == record.local) {
            existing = std::move(record);
            return validate();
        }
    }
    chunks.push_back(std::move(record));
    return validate();
}

core::Result<std::vector<std::uint8_t>>
ChunkRegionBinaryCodec::encode(const ChunkRegionFile& region) {
    auto status = region.validate();
    if (!status)
        return core::Result<std::vector<std::uint8_t>>::failure(status.error().code,
                                                                status.error().message);
    Writer writer;
    for (auto byte : magic)
        writer.u8(byte);
    writer.u32(version);
    writer.i64(region.coord.x);
    writer.i64(region.coord.y);
    writer.i64(region.coord.z);
    writer.u32(static_cast<std::uint32_t>(region.chunks.size()));
    for (const auto& chunk : region.chunks) {
        writer.u8(chunk.local.x);
        writer.u8(chunk.local.y);
        writer.u8(chunk.local.z);
        writer.u64(chunk.generation_stamp);
        writer.u64(chunk.save_revision);
        writer.u64(payload_hash(chunk.encoded_chunk));
        writer.text(chunk.encoded_chunk);
    }
    return core::Result<std::vector<std::uint8_t>>::success(std::move(writer).take());
}

core::Result<ChunkRegionFile> ChunkRegionBinaryCodec::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > max_region_file_bytes)
        return core::Result<ChunkRegionFile>::failure("chunk_region.file_too_large",
                                                      "chunk region file exceeds safety limit");
    Reader reader(bytes);
    for (auto expected : magic) {
        auto actual = reader.u8();
        if (!actual || actual.value() != expected)
            return core::Result<ChunkRegionFile>::failure("chunk_region.invalid_magic",
                                                          "chunk region magic is invalid");
    }
    auto file_version = reader.u32();
    auto x = reader.i64();
    auto y = reader.i64();
    auto z = reader.i64();
    auto count = reader.u32();
    if (!file_version || file_version.value() != version || !x || !y || !z || !count ||
        count.value() > chunk_region_capacity)
        return core::Result<ChunkRegionFile>::failure("chunk_region.invalid_header",
                                                      "chunk region header is invalid");
    ChunkRegionFile region{{x.value(), y.value(), z.value()}, {}};
    region.chunks.reserve(count.value());
    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto lx = reader.u8();
        auto ly = reader.u8();
        auto lz = reader.u8();
        auto generation = reader.u64();
        auto revision = reader.u64();
        auto expected_hash = reader.u64();
        auto payload = reader.text();
        if (!lx || !ly || !lz || !generation || !revision || !expected_hash || !payload)
            return core::Result<ChunkRegionFile>::failure("chunk_region.invalid_record",
                                                          "chunk region record is invalid");
        if (payload_hash(payload.value()) != expected_hash.value())
            return core::Result<ChunkRegionFile>::failure("chunk_region.checksum_mismatch",
                                                          "chunk region payload checksum mismatch");
        region.chunks.push_back({{lx.value(), ly.value(), lz.value()},
                                 generation.value(),
                                 revision.value(),
                                 std::move(payload).value()});
    }
    if (!reader.eof())
        return core::Result<ChunkRegionFile>::failure("chunk_region.trailing_bytes",
                                                      "chunk region has trailing bytes");
    auto status = region.validate();
    if (!status)
        return core::Result<ChunkRegionFile>::failure(status.error().code, status.error().message);
    return core::Result<ChunkRegionFile>::success(std::move(region));
}

ChunkRegionFileStore::ChunkRegionFileStore(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path ChunkRegionFileStore::path_for(ChunkRegionCoord coord) const {
    return root_ / ("r." + std::to_string(coord.x) + "." + std::to_string(coord.y) + "." +
                    std::to_string(coord.z) + ".hsr");
}

core::Status ChunkRegionFileStore::save(const ChunkRegionFile& region) const {
    auto encoded = ChunkRegionBinaryCodec::encode(region);
    if (!encoded)
        return core::Status::failure(encoded.error().code, encoded.error().message);
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        return core::Status::failure("chunk_region.create_directory_failed", error.message());
    const auto target = path_for(region.coord);
    const auto temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return core::Status::failure("chunk_region.open_failed",
                                         "failed to open temporary region file");
        output.write(reinterpret_cast<const char*>(encoded.value().data()),
                     static_cast<std::streamsize>(encoded.value().size()));
        if (!output)
            return core::Status::failure("chunk_region.write_failed",
                                         "failed to write region file");
    }
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary);
        return core::Status::failure("chunk_region.commit_failed", error.message());
    }
    return core::Status::ok();
}

core::Result<ChunkRegionFile> ChunkRegionFileStore::load(ChunkRegionCoord coord) const {
    const auto path = path_for(coord);
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return core::Result<ChunkRegionFile>::failure("chunk_region.stat_failed", error.message());
    if (size > max_region_file_bytes)
        return core::Result<ChunkRegionFile>::failure("chunk_region.file_too_large",
                                                      "chunk region file exceeds safety limit");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return core::Result<ChunkRegionFile>::failure("chunk_region.open_failed",
                                                      "failed to open region file");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty())
        return core::Result<ChunkRegionFile>::failure("chunk_region.read_failed",
                                                      "failed to read region file");
    auto decoded = ChunkRegionBinaryCodec::decode(bytes);
    if (!decoded)
        return decoded;
    if (decoded.value().coord != coord)
        return core::Result<ChunkRegionFile>::failure(
            "chunk_region.coord_mismatch",
            "region file coordinate does not match requested coordinate");
    return decoded;
}

} // namespace heartstead::world
