#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace heartstead::renderer {

struct StagingRingRange {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::uint64_t submission_serial = 0;

    [[nodiscard]] bool is_valid() const noexcept;
    friend bool operator==(const StagingRingRange&, const StagingRingRange&) = default;
};

struct StagingRingStats {
    std::size_t capacity_bytes = 0;
    std::size_t used_bytes = 0;
    std::size_t free_bytes = 0;
    std::size_t pending_range_count = 0;
    std::uint64_t wrap_count = 0;
};

class StagingRingAllocator {
  public:
    explicit StagingRingAllocator(std::size_t capacity_bytes);

    [[nodiscard]] core::Result<StagingRingRange>
    allocate(std::size_t size, std::size_t alignment, std::uint64_t submission_serial,
             std::uint64_t completed_submission_serial);
    [[nodiscard]] bool cancel(StagingRingRange range) noexcept;
    void release_completed(std::uint64_t completed_submission_serial) noexcept;
    void reset() noexcept;

    [[nodiscard]] StagingRingStats stats() const noexcept;

  private:
    struct Gap {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    [[nodiscard]] core::Result<std::size_t> allocate_from_gaps(std::size_t size,
                                                               std::size_t alignment);
    [[nodiscard]] std::vector<Gap> free_gaps() const;

    std::size_t capacity_ = 0;
    std::size_t head_ = 0;
    std::vector<StagingRingRange> active_;
    std::uint64_t wrap_count_ = 0;
};

} // namespace heartstead::renderer
