#pragma once

#include "BgfxResourceSlotGeneration.hpp"
#include "../RenderPostProcessValidation.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <unordered_map>
#include <vector>

namespace Tina::Render::Bgfx {

// One owner for logical render textures, their per-mip native storage and every
// framebuffer referring to it. Native mips are independent single-level images:
// D3D11's whole-texture SRV cannot safely sample one mip while another is an RTV.
// This also implements partial mip chains exactly, rather than bgfx's all-or-none
// createTexture2D mip flag silently allocating a different descriptor.
class BgfxRenderTextureResources final {
  public:
    BgfxRenderTextureResources() = default;
    BgfxRenderTextureResources(const BgfxRenderTextureResources&) = delete;
    BgfxRenderTextureResources& operator=(const BgfxRenderTextureResources&) = delete;

    [[nodiscard]] Core::Result<GpuRenderTextureId> create(u32 owner, const RenderTextureDesc& desc);
    [[nodiscard]] Core::Status validate(u32 owner, GpuRenderTextureId id) const noexcept;
    [[nodiscard]] Core::Status bind(u32 owner, u32 key, GpuRenderTextureId id) noexcept;
    [[nodiscard]] Core::Status clearBinding(u32 key) noexcept;
    [[nodiscard]] std::optional<Detail::RenderTextureResourceView> resolve(u32 key) const noexcept;
    [[nodiscard]] bgfx::TextureHandle texture(u32 key, u8 mip) const noexcept;
    [[nodiscard]] Core::Result<bgfx::FrameBufferHandle> framebuffer(u32 colorKey, u8 colorMip,
                                                                   u32 depthKey = 0, u8 depthMip = 0);
    [[nodiscard]] Core::Status retire(u32 owner, GpuRenderTextureId id, bool deferred) noexcept;
    [[nodiscard]] u32 beginRetirements() noexcept;
    [[nodiscard]] u32 completeRetirements() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] u64 liveCount() const noexcept { return liveCount_; }
    [[nodiscard]] u64 nativeCount() const noexcept { return nativeCount_; }
    [[nodiscard]] u64 destroyedCount() const noexcept { return destroyedCount_; }
    [[nodiscard]] u64 pendingRetirementCount() const noexcept;

  private:
    enum class Phase : u8 { None, Queued, Waiting };
    struct Mip final {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    };
    struct Slot final {
        BgfxTextureResourceSlotGeneration generation{};
        RenderTextureDesc desc{};
        std::array<Mip, RenderTextureDesc::MaximumMipCount> mips{};
        bool live = false;
        Phase retirement = Phase::None;
    };
    struct Framebuffer final {
        GpuRenderTextureId color{};
        GpuRenderTextureId depth{};
        u8 colorMip = 0;
        u8 depthMip = 0;
        bgfx::FrameBufferHandle handle = BGFX_INVALID_HANDLE;
        Phase retirement = Phase::None;
    };
    void destroyFramebuffer(Framebuffer& entry) noexcept;
    void destroySlot(Slot& slot) noexcept;

    std::vector<Slot> slots_{};
    std::unordered_map<u32, GpuRenderTextureId> bindings_{};
    std::vector<Framebuffer> framebuffers_{};
    u64 liveCount_ = 0;
    u64 nativeCount_ = 0;
    u64 destroyedCount_ = 0;
};

} // namespace Tina::Render::Bgfx
