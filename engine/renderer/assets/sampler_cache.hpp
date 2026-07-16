#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <cstddef>
#include <vector>

namespace heartstead::renderer {

struct SamplerCacheStats {
    std::size_t resident_sampler_count = 0;
    std::uint64_t cache_hit_count = 0;
    std::uint64_t cache_miss_count = 0;
};

class SamplerCache {
  public:
    explicit SamplerCache(rhi::IRenderDevice& device);
    ~SamplerCache();

    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;

    [[nodiscard]] core::Result<rhi::RenderResourceHandle> get(rhi::RenderSamplerDesc desc);
    [[nodiscard]] core::Status shutdown();
    [[nodiscard]] const SamplerCacheStats& stats() const noexcept;

  private:
    struct Entry {
        rhi::RenderSamplerDesc desc;
        rhi::RenderResourceHandle handle;
    };

    rhi::IRenderDevice& device_;
    std::vector<Entry> entries_;
    SamplerCacheStats stats_{};
};

[[nodiscard]] bool equivalent_sampler_desc(const rhi::RenderSamplerDesc& left,
                                           const rhi::RenderSamplerDesc& right) noexcept;

} // namespace heartstead::renderer
