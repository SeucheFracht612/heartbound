#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/dirty/dirty_region.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::rooms {

struct RoomIdTag;
using RoomId = core::StrongU64Id<RoomIdTag>;

enum class RoomDescriptorSeverity {
    positive,
    neutral,
    warning,
};

struct RoomDescriptor {
    std::string code;
    std::string label;
    RoomDescriptorSeverity severity = RoomDescriptorSeverity::neutral;
};

struct RoomMetrics {
    std::uint32_t enclosure_per_mille = 0;
    std::uint32_t roof_coverage_per_mille = 0;
    std::uint32_t wall_coverage_per_mille = 0;
    std::int32_t warmth = 0;
    std::int32_t dryness = 0;
    std::uint32_t light_per_mille = 0;
    std::uint32_t smoke_per_mille = 0;
    std::uint32_t ventilation_per_mille = 0;
    std::uint32_t safety_per_mille = 0;
    std::uint32_t spaciousness_per_mille = 0;
    bool storage_access = false;
    bool cart_access = false;
    bool power_access = false;
    bool ward_coverage = false;
    bool terrain_contact = false;
    bool underground = false;
    std::uint32_t weather_exposure_per_mille = 0;
    std::uint32_t dampness_per_mille = 0;
    std::uint32_t cleanliness_per_mille = 1000;
};

struct RoomRecord {
    RoomId id;
    std::string label;
    std::uint32_t volume_cells = 0;
    std::vector<core::SaveId> source_build_piece_ids;
    RoomMetrics metrics;
    std::vector<RoomDescriptor> descriptors;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool has_descriptor(std::string_view code) const noexcept;
};

class RoomEvaluator {
  public:
    [[nodiscard]] static std::vector<RoomDescriptor> evaluate(const RoomMetrics& metrics);
};

class RoomGraph {
  public:
    [[nodiscard]] core::Status add_or_replace(RoomRecord room);
    [[nodiscard]] const RoomRecord* find(RoomId id) const noexcept;
    [[nodiscard]] std::vector<const RoomRecord*> rooms() const;
    [[nodiscard]] std::size_t room_count() const noexcept;
    [[nodiscard]] std::size_t count_descriptor(std::string_view code) const noexcept;

    void evaluate_all();
    void mark_dirty() noexcept;
    [[nodiscard]] core::Status mark_dirty_region(dirty::DirtyRegionTracker& dirty_regions,
                                                 dirty::DirtyRegionBounds bounds,
                                                 std::string reason);
    void clear_dirty() noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;

  private:
    std::unordered_map<std::uint64_t, RoomRecord> rooms_;
    bool dirty_ = false;
};

[[nodiscard]] std::string_view
room_descriptor_severity_name(RoomDescriptorSeverity severity) noexcept;
[[nodiscard]] core::Result<RoomDescriptorSeverity>
room_descriptor_severity_from_name(std::string_view name);

} // namespace heartstead::rooms
