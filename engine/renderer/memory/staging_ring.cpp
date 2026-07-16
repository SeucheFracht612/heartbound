#include "engine/renderer/memory/staging_ring.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>

namespace heartstead::renderer {

namespace {

[[nodiscard]] core::Result<std::size_t> align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return core::Result<std::size_t>::failure("staging_ring.invalid_alignment",
                                                  "staging alignment must be nonzero");
    }
    const auto remainder = value % alignment;
    if (remainder == 0) {
        return core::Result<std::size_t>::success(value);
    }
    const auto padding = alignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - padding) {
        return core::Result<std::size_t>::failure("staging_ring.alignment_overflow",
                                                  "staging alignment overflows size_t");
    }
    return core::Result<std::size_t>::success(value + padding);
}

} // namespace

bool StagingRingRange::is_valid() const noexcept {
    return size > 0 && submission_serial > 0;
}

StagingRingAllocator::StagingRingAllocator(std::size_t capacity_bytes)
    : capacity_(capacity_bytes) {}

core::Result<StagingRingRange>
StagingRingAllocator::allocate(std::size_t size, std::size_t alignment,
                               std::uint64_t submission_serial,
                               std::uint64_t completed_submission_serial) {
    release_completed(completed_submission_serial);
    if (capacity_ == 0 || size == 0 || size > capacity_ || submission_serial == 0) {
        return core::Result<StagingRingRange>::failure(
            "staging_ring.invalid_allocation",
            "staging allocation must fit the ring and use a nonzero submission serial");
    }
    auto offset = allocate_from_gaps(size, alignment);
    if (!offset) {
        return core::Result<StagingRingRange>::failure(offset.error().code, offset.error().message);
    }
    const auto previous_head = head_;
    StagingRingRange range{offset.value(), size, submission_serial};
    active_.push_back(range);
    head_ = range.offset + range.size;
    if (head_ == capacity_) {
        head_ = 0;
    }
    if (range.offset < previous_head) {
        ++wrap_count_;
    }
    return core::Result<StagingRingRange>::success(range);
}

bool StagingRingAllocator::cancel(StagingRingRange range) noexcept {
    const auto found = std::ranges::find(active_, range);
    if (found == active_.end()) {
        return false;
    }
    active_.erase(found);
    return true;
}

void StagingRingAllocator::release_completed(std::uint64_t completed_submission_serial) noexcept {
    std::erase_if(active_, [completed_submission_serial](const StagingRingRange& range) {
        return range.submission_serial <= completed_submission_serial;
    });
}

void StagingRingAllocator::reset() noexcept {
    head_ = 0;
    active_.clear();
    wrap_count_ = 0;
}

StagingRingStats StagingRingAllocator::stats() const noexcept {
    StagingRingStats result;
    result.capacity_bytes = capacity_;
    result.pending_range_count = active_.size();
    result.wrap_count = wrap_count_;
    for (const auto& range : active_) {
        result.used_bytes += range.size;
    }
    result.free_bytes = capacity_ - result.used_bytes;
    return result;
}

core::Result<std::size_t> StagingRingAllocator::allocate_from_gaps(std::size_t size,
                                                                   std::size_t alignment) {
    const auto gaps = free_gaps();
    const auto try_gap = [size, alignment](Gap gap, std::size_t minimum) {
        const auto start = std::max(gap.begin, minimum);
        auto aligned = align_up(start, alignment);
        if (!aligned || aligned.value() > gap.end || size > gap.end - aligned.value()) {
            return std::optional<std::size_t>{};
        }
        return std::optional<std::size_t>{aligned.value()};
    };
    for (const auto gap : gaps) {
        if (gap.end <= head_) {
            continue;
        }
        if (auto offset = try_gap(gap, head_)) {
            return core::Result<std::size_t>::success(*offset);
        }
    }
    for (const auto gap : gaps) {
        if (gap.begin >= head_) {
            continue;
        }
        const Gap before_head{gap.begin, std::min(gap.end, head_)};
        if (auto offset = try_gap(before_head, before_head.begin)) {
            return core::Result<std::size_t>::success(*offset);
        }
    }
    return core::Result<std::size_t>::failure("staging_ring.full",
                                              "no completed staging range can fit the upload");
}

std::vector<StagingRingAllocator::Gap> StagingRingAllocator::free_gaps() const {
    auto active = active_;
    std::ranges::sort(active, {}, &StagingRingRange::offset);
    std::vector<Gap> gaps;
    std::size_t cursor = 0;
    for (const auto& range : active) {
        if (cursor < range.offset) {
            gaps.push_back({cursor, range.offset});
        }
        cursor = std::max(cursor, range.offset + range.size);
    }
    if (cursor < capacity_) {
        gaps.push_back({cursor, capacity_});
    }
    return gaps;
}

} // namespace heartstead::renderer
