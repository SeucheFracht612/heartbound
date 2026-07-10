#include "engine/rooms/room_graph.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::rooms {

namespace {

[[nodiscard]] bool per_mille_valid(std::uint32_t value) noexcept {
    return value <= 1000;
}

void add_descriptor(std::vector<RoomDescriptor>& descriptors, std::string code, std::string label,
                    RoomDescriptorSeverity severity) {
    descriptors.push_back(RoomDescriptor{std::move(code), std::move(label), severity});
}

} // namespace

core::Status RoomRecord::validate() const {
    if (!id.is_valid()) {
        return core::Status::failure("room.invalid_id", "room id must be valid");
    }
    if (volume_cells == 0) {
        return core::Status::failure("room.invalid_volume", "room volume must be non-zero");
    }
    if (!per_mille_valid(metrics.enclosure_per_mille) ||
        !per_mille_valid(metrics.roof_coverage_per_mille) ||
        !per_mille_valid(metrics.wall_coverage_per_mille) ||
        !per_mille_valid(metrics.light_per_mille) || !per_mille_valid(metrics.smoke_per_mille) ||
        !per_mille_valid(metrics.ventilation_per_mille) ||
        !per_mille_valid(metrics.safety_per_mille) ||
        !per_mille_valid(metrics.spaciousness_per_mille) ||
        !per_mille_valid(metrics.weather_exposure_per_mille) ||
        !per_mille_valid(metrics.dampness_per_mille) ||
        !per_mille_valid(metrics.cleanliness_per_mille)) {
        return core::Status::failure("room.invalid_metric",
                                     "room per-mille metrics must be 0..1000");
    }
    return core::Status::ok();
}

bool RoomRecord::has_descriptor(std::string_view code) const noexcept {
    return std::ranges::any_of(
        descriptors, [code](const RoomDescriptor& descriptor) { return descriptor.code == code; });
}

std::vector<RoomDescriptor> RoomEvaluator::evaluate(const RoomMetrics& metrics) {
    std::vector<RoomDescriptor> descriptors;

    if (metrics.enclosure_per_mille >= 850 && metrics.roof_coverage_per_mille >= 800) {
        add_descriptor(descriptors, "enclosed", "Enclosed Room", RoomDescriptorSeverity::positive);
    } else {
        add_descriptor(descriptors, "exposed", "Exposed Room", RoomDescriptorSeverity::warning);
    }

    if (metrics.warmth >= 250) {
        add_descriptor(descriptors, "warm", "Warm", RoomDescriptorSeverity::positive);
    } else if (metrics.warmth <= -250) {
        add_descriptor(descriptors, "cold", "Cold", RoomDescriptorSeverity::warning);
    }

    if (metrics.dryness >= 250) {
        add_descriptor(descriptors, "dry", "Dry", RoomDescriptorSeverity::positive);
    } else if (metrics.dryness <= -250) {
        add_descriptor(descriptors, "damp", "Damp", RoomDescriptorSeverity::warning);
    }

    if (metrics.smoke_per_mille >= 500) {
        add_descriptor(descriptors, "smoky", "Smoky", RoomDescriptorSeverity::warning);
    } else if (metrics.ventilation_per_mille >= 650) {
        add_descriptor(descriptors, "ventilated", "Ventilated", RoomDescriptorSeverity::positive);
    }

    if (metrics.light_per_mille <= 200) {
        add_descriptor(descriptors, "dark", "Dark", RoomDescriptorSeverity::neutral);
    } else if (metrics.light_per_mille >= 750) {
        add_descriptor(descriptors, "bright", "Bright", RoomDescriptorSeverity::positive);
    }

    if (metrics.safety_per_mille < 500) {
        add_descriptor(descriptors, "unsafe", "Unsafe", RoomDescriptorSeverity::warning);
    }
    if (metrics.spaciousness_per_mille < 350) {
        add_descriptor(descriptors, "crowded", "Crowded", RoomDescriptorSeverity::warning);
    } else if (metrics.spaciousness_per_mille >= 750) {
        add_descriptor(descriptors, "spacious", "Spacious", RoomDescriptorSeverity::positive);
    }

    if (metrics.storage_access) {
        add_descriptor(descriptors, "storage_access", "Storage Access",
                       RoomDescriptorSeverity::positive);
    }
    if (metrics.cart_access) {
        add_descriptor(descriptors, "cart_access", "Cart Access", RoomDescriptorSeverity::positive);
    } else {
        add_descriptor(descriptors, "poor_cart_access", "Poor Cart Access",
                       RoomDescriptorSeverity::warning);
    }
    if (metrics.power_access) {
        add_descriptor(descriptors, "power_access", "Power Access",
                       RoomDescriptorSeverity::positive);
    }
    if (metrics.ward_coverage) {
        add_descriptor(descriptors, "warded", "Warded", RoomDescriptorSeverity::positive);
    }
    if (metrics.underground && metrics.warmth <= 100 && metrics.dampness_per_mille < 400) {
        add_descriptor(descriptors, "cool_cellar", "Cool Cellar", RoomDescriptorSeverity::positive);
    }
    if (metrics.terrain_contact && metrics.ward_coverage && metrics.safety_per_mille >= 700) {
        add_descriptor(descriptors, "stable_ward_room", "Stable Ward Room",
                       RoomDescriptorSeverity::positive);
    }
    if (metrics.terrain_contact && metrics.storage_access && metrics.dryness >= 250 &&
        metrics.weather_exposure_per_mille <= 200) {
        add_descriptor(descriptors, "dry_fuel_shed", "Dry Fuel Shed",
                       RoomDescriptorSeverity::positive);
    }
    if (metrics.cleanliness_per_mille < 350) {
        add_descriptor(descriptors, "dirty", "Dirty", RoomDescriptorSeverity::warning);
    }
    if (metrics.weather_exposure_per_mille > 500) {
        add_descriptor(descriptors, "weather_exposed", "Weather Exposed",
                       RoomDescriptorSeverity::warning);
    }

    return descriptors;
}

core::Status RoomGraph::add_or_replace(RoomRecord room) {
    auto status = room.validate();
    if (!status) {
        return status;
    }
    rooms_[room.id.value()] = std::move(room);
    mark_dirty();
    return core::Status::ok();
}

const RoomRecord* RoomGraph::find(RoomId id) const noexcept {
    const auto found = rooms_.find(id.value());
    return found == rooms_.end() ? nullptr : &found->second;
}

std::vector<const RoomRecord*> RoomGraph::rooms() const {
    std::vector<const RoomRecord*> result;
    result.reserve(rooms_.size());
    for (const auto& [_, room] : rooms_) {
        result.push_back(&room);
    }
    return result;
}

std::size_t RoomGraph::room_count() const noexcept {
    return rooms_.size();
}

std::size_t RoomGraph::count_descriptor(std::string_view code) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        rooms_, [code](const auto& entry) { return entry.second.has_descriptor(code); }));
}

void RoomGraph::evaluate_all() {
    for (auto& [_, room] : rooms_) {
        room.descriptors = RoomEvaluator::evaluate(room.metrics);
    }
    clear_dirty();
}

void RoomGraph::mark_dirty() noexcept {
    dirty_ = true;
}

core::Status RoomGraph::mark_dirty_region(dirty::DirtyRegionTracker& dirty_regions,
                                          dirty::DirtyRegionBounds bounds, std::string reason) {
    mark_dirty();
    return dirty_regions.mark(dirty::DirtyRegionKind::room_graph, bounds, std::move(reason));
}

void RoomGraph::clear_dirty() noexcept {
    dirty_ = false;
}

bool RoomGraph::is_dirty() const noexcept {
    return dirty_;
}

std::string_view room_descriptor_severity_name(RoomDescriptorSeverity severity) noexcept {
    switch (severity) {
    case RoomDescriptorSeverity::positive:
        return "positive";
    case RoomDescriptorSeverity::neutral:
        return "neutral";
    case RoomDescriptorSeverity::warning:
        return "warning";
    }
    return "unknown";
}

core::Result<RoomDescriptorSeverity> room_descriptor_severity_from_name(std::string_view name) {
    if (name == "positive") {
        return core::Result<RoomDescriptorSeverity>::success(RoomDescriptorSeverity::positive);
    }
    if (name == "neutral") {
        return core::Result<RoomDescriptorSeverity>::success(RoomDescriptorSeverity::neutral);
    }
    if (name == "warning") {
        return core::Result<RoomDescriptorSeverity>::success(RoomDescriptorSeverity::warning);
    }
    return core::Result<RoomDescriptorSeverity>::failure(
        "room_descriptor.invalid_severity",
        "room descriptor severity must be positive, neutral, or warning");
}

} // namespace heartstead::rooms
