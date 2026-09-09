#include "PrimaryPostProcess.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace Tina::Runtime::Detail {

Core::Status PrimaryPostProcess::drainRetired(Render::IRenderDevice& device) noexcept
{
    while (!m_retired.empty()) {
        if (auto status = device.destroyRenderTexture(m_retired.back()); !status) { return status; }
        m_retired.pop_back();
    }
    return Core::success();
}

Core::Status PrimaryPostProcess::shutdown(Render::IRenderDevice& device) noexcept
{
    if (auto status = drainRetired(device); !status) { return status; }
    for (Target& target : m_targets) {
        if (!target.id) { continue; }
        if (auto status = device.destroyRenderTexture(target.id); !status) { return status; }
        target = {};
    }
    m_width = m_height = 0;
    m_bloomMips = 0;
    m_scratchCount = 0;
    return Core::success();
}

Core::Result<Render::RenderPostProcessChainView> PrimaryPostProcess::prepare(
    Render::IRenderDevice& device, const std::optional<Render::RenderSurfaceState>& surface,
    const Render::PrimaryPostProcessSettings& settings, Render::FrameResourceTableView resources)
try {
    using namespace Render;
    if (auto status = validatePrimaryPostProcessSettings(settings); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = drainRetired(device); !status) { return Core::failure(std::move(status.error())); }
    if (!settings.enabled) {
        if (auto status = shutdown(device); !status) { return Core::failure(std::move(status.error())); }
        return RenderPostProcessChainView{};
    }
    // A suspended surface does not allocate zero-size resources or release the
    // last usable set. Native rebind likewise does not recreate device resources.
    if (!surface || surface->availability != RenderSurfaceAvailability::Active) {
        return RenderPostProcessChainView{};
    }
    // Resolve borrowed refs before acquiring resources. A stale packet must not
    // silently resolve the same integer key belonging to another frame.
    std::array<RenderPostProcessStep, PrimaryPostProcessSettings::MaximumCustomEffectCount> steps{};
    const auto bindingKey = [&](FrameResourceRef reference, FrameResourceKind kind) -> Core::Result<Core::u32> {
        const auto* descriptor = resources.resolve(reference, kind);
        if (descriptor == nullptr || descriptor->deviceBindingKey == 0 ||
            descriptor->deviceBindingKey > (std::numeric_limits<Core::u32>::max)())
            return Core::failure(RenderErrorCode::InvalidFrameResource,
                                 "Primary post-process effect contains a stale, wrong-kind or cross-packet resource");
        return static_cast<Core::u32>(descriptor->deviceBindingKey);
    };
    for (Core::u8 index = 0; index < settings.customEffectCount; ++index) {
        const auto& effect = settings.customEffects[index];
        auto shader = bindingKey(effect.shader, FrameResourceKind::Shader);
        if (!shader) { return Core::failure(std::move(shader.error())); }
        steps[index].kind = RenderPostProcessStepKind::CustomShader;
        steps[index].shaderBindingKey = *shader;
        if (effect.shaderUniforms) {
            auto uniforms = bindingKey(effect.shaderUniforms, FrameResourceKind::ShaderUniforms);
            if (!uniforms) { return Core::failure(std::move(uniforms.error())); }
            steps[index].shaderUniformBindingKey = *uniforms;
        }
    }
    const auto extent = surface->framebufferExtent;
    if (extent.width == 0 || extent.height == 0 ||
        extent.width > RenderTextureDesc::MaximumDimension ||
        extent.height > RenderTextureDesc::MaximumDimension) {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "Primary post-process framebuffer dimensions are unsupported");
    }
    const auto width = static_cast<Core::u16>(extent.width);
    const auto height = static_cast<Core::u16>(extent.height);
    const auto bloomWidth = static_cast<Core::u16>((std::max)(1U, extent.width / 2U));
    const auto bloomHeight = static_cast<Core::u16>((std::max)(1U, extent.height / 2U));
    Core::u8 availableMips = 1;
    for (Core::u32 edge = (std::max)(bloomWidth, bloomHeight); edge > 1U; edge /= 2U) { ++availableMips; }
    const Core::u8 bloomMips = settings.bloom.enabled
        ? (std::min)(settings.bloom.mipCount, availableMips) : 0;
    const Core::u8 scratchCount = (std::min)(settings.customEffectCount, MaximumEffectTargets);
    if (m_width != width || m_height != height || m_bloomMips != bloomMips ||
        m_scratchCount != scratchCount) {
        // Reserve tracking before acquiring any native resource. Candidate
        // targets remain tracked on every early return; old bindings stay live.
        m_retired.reserve(m_targets.size() * 2U);
        std::array<Target, TargetCount> candidate{};
        for (Core::usize index = 0; index < candidate.size(); ++index) {
            const bool depth = index == SceneDepth;
            const bool bloom = index == BloomDownsample || index == BloomUpsample;
            if ((bloom && bloomMips == 0) ||
                (index >= EffectFirst && index - EffectFirst >= scratchCount)) continue;
            auto target = device.createRenderTexture({
                .width = bloom ? bloomWidth : width,
                .height = bloom ? bloomHeight : height,
                .format = depth ? RenderTextureFormat::Depth32Float : RenderTextureFormat::Rgba16Float,
                .usage = (depth ? RenderTextureUsage::DepthStencilAttachment : RenderTextureUsage::ColorAttachment)
                         | RenderTextureUsage::Sampled,
                .mipCount = bloom ? bloomMips : Core::u8{1},
            });
            if (!target) { return Core::failure(std::move(target.error())); }
            m_retired.push_back(*target);
            auto key = device.createRenderTextureBinding(*target);
            if (!key) { return Core::failure(std::move(key.error())); }
            candidate[index] = {*target, *key};
        }
        m_retired.clear();
        for (const Target& target : m_targets) {
            if (target.id) { m_retired.push_back(target.id); }
        }
        m_targets = candidate;
        m_width = width;
        m_height = height;
        m_bloomMips = bloomMips;
        m_scratchCount = scratchCount;
    }
    Core::u32 sourceKey = m_targets[SceneColor].key;
    for (Core::u8 index = 0; index < settings.customEffectCount; ++index) {
        steps[index].sourceBindingKey = sourceKey;
        steps[index].destinationBindingKey = m_targets[EffectFirst + index % MaximumEffectTargets].key;
        sourceKey = steps[index].destinationBindingKey;
    }
    m_customSteps = steps;
    BloomDesc bloom = settings.bloom;
    if (bloom.enabled) { bloom.mipCount = m_bloomMips; }
    return RenderPostProcessChainView{
        .sceneColorTargetBindingKey = m_targets[SceneColor].key,
        .sceneDepthTargetBindingKey = m_targets[SceneDepth].key,
        .bloomDownsampleTargetBindingKey = m_targets[BloomDownsample].key,
        .bloomUpsampleTargetBindingKey = m_targets[BloomUpsample].key,
        .fog = settings.fog,
        .bloom = bloom,
        .toneMapping = settings.toneMapping,
        .customSteps = std::span{m_customSteps}.first(settings.customEffectCount),
    };
} catch (const std::bad_alloc&) {
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Primary post-process target staging allocation failed");
}

} // namespace Tina::Runtime::Detail
