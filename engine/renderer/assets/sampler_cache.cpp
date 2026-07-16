#include "engine/renderer/assets/sampler_cache.hpp"

#include <ranges>
#include <utility>

namespace heartstead::renderer {

SamplerCache::SamplerCache(rhi::IRenderDevice& device) : device_(device) {}

SamplerCache::~SamplerCache() {
    (void)shutdown();
}

core::Result<rhi::RenderResourceHandle> SamplerCache::get(rhi::RenderSamplerDesc desc) {
    const auto found = std::ranges::find_if(entries_, [&desc](const Entry& entry) {
        return equivalent_sampler_desc(entry.desc, desc);
    });
    if (found != entries_.end()) {
        ++stats_.cache_hit_count;
        return core::Result<rhi::RenderResourceHandle>::success(found->handle);
    }
    ++stats_.cache_miss_count;
    auto created = device_.create_sampler(desc);
    if (!created) {
        return core::Result<rhi::RenderResourceHandle>::failure(created.error().code,
                                                                created.error().message);
    }
    entries_.push_back({std::move(desc), created.value().handle});
    stats_.resident_sampler_count = entries_.size();
    return core::Result<rhi::RenderResourceHandle>::success(created.value().handle);
}

core::Status SamplerCache::shutdown() {
    core::Status first_failure = core::Status::ok();
    for (auto& entry : entries_) {
        auto status = device_.release_resource(entry.handle);
        if (!status && first_failure) {
            first_failure = status;
        }
        entry.handle = {};
    }
    entries_.clear();
    stats_.resident_sampler_count = 0;
    return first_failure;
}

const SamplerCacheStats& SamplerCache::stats() const noexcept {
    return stats_;
}

bool equivalent_sampler_desc(const rhi::RenderSamplerDesc& left,
                             const rhi::RenderSamplerDesc& right) noexcept {
    return left.min_filter == right.min_filter && left.mag_filter == right.mag_filter &&
           left.mipmap_mode == right.mipmap_mode && left.address_u == right.address_u &&
           left.address_v == right.address_v && left.address_w == right.address_w &&
           left.max_anisotropy == right.max_anisotropy && left.min_lod == right.min_lod &&
           left.max_lod == right.max_lod;
}

} // namespace heartstead::renderer
