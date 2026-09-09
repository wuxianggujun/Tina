#pragma once

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderSurface.hpp>

#include <array>
#include <vector>

namespace Tina::Runtime::Detail {

// EngineHost-owned target set. Device shutdown is the final owner boundary;
// failed preparation/retirement keeps every live handle here for retry.
class PrimaryPostProcess final {
  public:
    PrimaryPostProcess() = default;
    PrimaryPostProcess(const PrimaryPostProcess&) = delete;
    PrimaryPostProcess& operator=(const PrimaryPostProcess&) = delete;

    [[nodiscard]] Core::Result<Render::RenderPostProcessChainView> prepare(
        Render::IRenderDevice& device, const std::optional<Render::RenderSurfaceState>& surface,
        const Render::PrimaryPostProcessSettings& settings, Render::FrameResourceTableView resources);
    [[nodiscard]] Core::Status shutdown(Render::IRenderDevice& device) noexcept;

  private:
    enum TargetIndex : Core::usize {
        SceneColor, SceneDepth, BloomDownsample, BloomUpsample,
        EffectFirst, EffectSecond, TargetCount,
    };
    static constexpr Core::u8 MaximumEffectTargets = 2;

    struct Target final {
        Render::GpuRenderTextureId id{};
        Core::u32 key = 0;
    };
    [[nodiscard]] Core::Status drainRetired(Render::IRenderDevice& device) noexcept;
    std::array<Target, TargetCount> m_targets{};
    std::vector<Render::GpuRenderTextureId> m_retired{};
    std::array<Render::RenderPostProcessStep,
               Render::PrimaryPostProcessSettings::MaximumCustomEffectCount> m_customSteps{};
    Core::u16 m_width = 0;
    Core::u16 m_height = 0;
    Core::u8 m_bloomMips = 0;
    Core::u8 m_scratchCount = 0;
};

} // namespace Tina::Runtime::Detail
