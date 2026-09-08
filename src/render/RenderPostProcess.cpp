#include <tina/render/RenderPostProcess.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Render {
namespace {

[[nodiscard]] constexpr bool isKnownRenderTextureFormat(RenderTextureFormat format) noexcept
{
    switch (format)
    {
    case RenderTextureFormat::Rgba8Unorm:
    case RenderTextureFormat::Rgba8Srgb:
    case RenderTextureFormat::Rgba16Float:
    case RenderTextureFormat::Rg16Float:
    case RenderTextureFormat::R16Float:
    case RenderTextureFormat::Depth24Stencil8:
    case RenderTextureFormat::Depth32Float:
        return true;
    case RenderTextureFormat::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] constexpr bool isDepthFormat(RenderTextureFormat format) noexcept
{
    return format == RenderTextureFormat::Depth24Stencil8 ||
           format == RenderTextureFormat::Depth32Float;
}

[[nodiscard]] constexpr u8 fullMipCount(u16 width, u16 height) noexcept
{
    u8 count = 1;
    while (width > 1U || height > 1U)
    {
        width = width > 1U ? static_cast<u16>(width / 2U) : u16{1};
        height = height > 1U ? static_cast<u16>(height / 2U) : u16{1};
        ++count;
    }
    return count;
}

[[nodiscard]] constexpr bool isKnownToneMappingOperator(ToneMappingOperator operation) noexcept
{
    switch (operation)
    {
    case ToneMappingOperator::None:
    case ToneMappingOperator::Reinhard:
    case ToneMappingOperator::AcesFitted:
    case ToneMappingOperator::AgXApproximation:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isKnownFogMode(FogMode mode) noexcept
{
    switch (mode)
    {
    case FogMode::Linear:
    case FogMode::Exponential:
    case FogMode::ExponentialSquared:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isKnownPostProcessStepKind(RenderPostProcessStepKind kind) noexcept
{
    switch (kind)
    {
    case RenderPostProcessStepKind::Copy:
    case RenderPostProcessStepKind::CustomShader:
        return true;
    }
    return false;
}

[[nodiscard]] bool finiteUnit(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] float clampFiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) ? (std::max)(value, 0.0F) : 0.0F;
}

[[nodiscard]] float toneMapChannel(float value, ToneMappingOperator operation) noexcept
{
    value = clampFiniteNonNegative(value);
    switch (operation)
    {
    case ToneMappingOperator::None:
        return value;
    case ToneMappingOperator::Reinhard:
        return value / (1.0F + value);
    case ToneMappingOperator::AcesFitted:
    {
        constexpr float A = 2.51F;
        constexpr float B = 0.03F;
        constexpr float C = 2.43F;
        constexpr float D = 0.59F;
        constexpr float E = 0.14F;
        return std::clamp((value * (A * value + B)) /
                              (value * (C * value + D) + E),
                          0.0F, 1.0F);
    }
    case ToneMappingOperator::AgXApproximation:
    {
        // Compact sigmoid approximation with a stable shoulder. The GPU path
        // uses the same constants so reference pixels stay comparable.
        const float x = value / (value + 0.6F);
        return std::clamp(x * x * (3.0F - 2.0F * x), 0.0F, 1.0F);
    }
    }
    return 0.0F;
}

} // namespace

Core::Status validateRenderTextureDesc(const RenderTextureDesc& desc) noexcept
{
    if (desc.width == 0 || desc.height == 0 ||
        desc.width > RenderTextureDesc::MaximumDimension ||
        desc.height > RenderTextureDesc::MaximumDimension)
    {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "RenderTexture dimensions are out of range");
    }
    if (!isKnownRenderTextureFormat(desc.format) || desc.usage == RenderTextureUsage::None)
    {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "RenderTexture format and usage are required");
    }
    constexpr RenderTextureUsage KnownUsage =
        RenderTextureUsage::ColorAttachment |
        RenderTextureUsage::DepthStencilAttachment |
        RenderTextureUsage::Sampled |
        RenderTextureUsage::TransferSource |
        RenderTextureUsage::TransferDestination;
    if ((static_cast<u16>(desc.usage) & ~static_cast<u16>(KnownUsage)) != 0U)
    {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "RenderTexture usage contains unknown flags");
    }
    const bool depth = isDepthFormat(desc.format);
    if (depth == hasRenderTextureUsage(desc.usage, RenderTextureUsage::ColorAttachment) ||
        depth != hasRenderTextureUsage(desc.usage, RenderTextureUsage::DepthStencilAttachment))
    {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "RenderTexture format does not match its attachment usage");
    }
    if (desc.mipCount == 0 || desc.mipCount > RenderTextureDesc::MaximumMipCount ||
        desc.mipCount > fullMipCount(desc.width, desc.height) ||
        (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4 &&
         desc.sampleCount != 8 && desc.sampleCount != 16) ||
        (desc.sampleCount != 1 && desc.mipCount != 1))
    {
        return Core::failure(RenderErrorCode::InvalidRenderTexture,
                             "RenderTexture mip or sample count is invalid");
    }
    return Core::success();
}

Core::Status validateRenderPostProcessChain(const RenderPostProcessChainView& chain) noexcept
{
    if (!isKnownToneMappingOperator(chain.toneMapping.operation) ||
        !isKnownFogMode(chain.fog.mode))
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Post-process chain contains an unknown effect mode");
    }
    if (!chain.enabled())
    {
        return Core::success();
    }
    if (chain.offscreenPasses.size() > 16U || chain.decals.size() > 4096U ||
        chain.customSteps.size() > 16U)
    {
        return Core::failure(RenderErrorCode::PostProcessCapacityExceeded,
                             "Render post-process chain exceeds fixed capacities");
    }
    if ((chain.fog.enabled || chain.bloom.enabled || !chain.decals.empty()) &&
        chain.sceneColorTargetBindingKey == 0)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Scene effects require a non-zero offscreen scene color target");
    }
    if (chain.sceneColorTargetBindingKey == 0 && chain.sceneDepthTargetBindingKey != 0)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "A scene depth target requires an offscreen scene color target");
    }
    if ((chain.fog.enabled || !chain.decals.empty()) && chain.sceneDepthTargetBindingKey == 0)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Fog and decals require a sampled scene depth target");
    }
    if (chain.bloom.enabled &&
        (chain.bloomDownsampleTargetBindingKey == 0 || chain.bloomUpsampleTargetBindingKey == 0 ||
         chain.bloomDownsampleTargetBindingKey == chain.bloomUpsampleTargetBindingKey ||
         chain.bloomDownsampleTargetBindingKey == chain.sceneColorTargetBindingKey ||
         chain.bloomUpsampleTargetBindingKey == chain.sceneColorTargetBindingKey))
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Bloom requires distinct scene, downsample and upsample RenderTextures");
    }
    if (!std::isfinite(chain.toneMapping.exposure) || chain.toneMapping.exposure <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Tone mapping exposure must be positive and finite");
    }
    if (!std::isfinite(chain.bloom.threshold) || chain.bloom.threshold < 0.0F ||
        !finiteUnit(chain.bloom.softKnee) || !std::isfinite(chain.bloom.intensity) ||
        chain.bloom.intensity < 0.0F || chain.bloom.mipCount == 0 ||
        chain.bloom.mipCount > 10)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Bloom parameters are invalid");
    }
    if (!finiteUnit(chain.fog.colorR) || !finiteUnit(chain.fog.colorG) ||
        !finiteUnit(chain.fog.colorB) || !std::isfinite(chain.fog.density) ||
        chain.fog.density < 0.0F || !std::isfinite(chain.fog.linearStart) ||
        !std::isfinite(chain.fog.linearEnd) || chain.fog.linearStart < 0.0F ||
        chain.fog.linearEnd <= chain.fog.linearStart)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Fog parameters are invalid");
    }
    for (usize index = 0; index < chain.offscreenPasses.size(); ++index)
    {
        const RenderOffscreenPassView& pass = chain.offscreenPasses[index];
        if (pass.stablePassKey == 0 || pass.colorTargetBindingKey == 0 ||
            (pass.clearDepth && pass.depthTargetBindingKey == 0) ||
            !finiteUnit(pass.clearR) || !finiteUnit(pass.clearG) ||
            !finiteUnit(pass.clearB) || !finiteUnit(pass.clearA))
        {
            return Core::failure(RenderErrorCode::InvalidOffscreenPass,
                                 "Offscreen pass identity, target, or clear color is invalid");
        }
        for (usize previous = 0; previous < index; ++previous)
        {
            if (chain.offscreenPasses[previous].stablePassKey == pass.stablePassKey)
            {
                return Core::failure(RenderErrorCode::InvalidOffscreenPass,
                                     "Offscreen pass keys must be unique within a frame");
            }
        }
    }
    for (const RenderDecal& decal : chain.decals)
    {
        if (decal.materialBindingKey == 0 || !finiteUnit(decal.opacity) ||
            !std::all_of(decal.worldFromDecal.begin(), decal.worldFromDecal.end(),
                         [](float value) { return std::isfinite(value); }))
        {
            return Core::failure(RenderErrorCode::InvalidDecal,
                                 "Decal transform, opacity, or material binding is invalid");
        }
    }
    for (const RenderPostProcessStep& step : chain.customSteps)
    {
        if (!isKnownPostProcessStepKind(step.kind) ||
            step.sourceBindingKey == 0 || step.destinationBindingKey == 0 ||
            (step.sourceBindingKey == step.destinationBindingKey &&
             step.sourceMipLevel == step.destinationMipLevel) ||
            (step.kind == RenderPostProcessStepKind::CustomShader &&
             step.shaderBindingKey == 0) ||
            (step.kind == RenderPostProcessStepKind::Copy &&
             (step.shaderBindingKey != 0 || step.shaderUniformBindingKey != 0)))
        {
            return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                                 "Post-process steps require distinct offscreen subresources and a matching shader kind");
        }
    }
    return Core::success();
}

Core::Result<RenderPipelineSchedule>
buildRenderPipelineSchedule(const RenderPostProcessChainView& chain,
                            bool hasPrimaryWindowUI) noexcept
{
    if (auto status = validateRenderPostProcessChain(chain); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    RenderPipelineSchedule schedule{};
    // A chain nobody opted into plans nothing. Without this early return the default
    // ToneMappingOperator::AcesFitted would append a tone-mapping pass writing to the
    // primary surface for every frame that never mentioned post processing.
    if (!chain.enabled())
    {
        return schedule;
    }
    const auto append = [&schedule](RenderPipelinePassPlan plan) noexcept {
        if (schedule.m_passCount >= RenderPipelineSchedule::MaximumPassCount)
        {
            return false;
        }
        schedule.m_passes[schedule.m_passCount++] = plan;
        return true;
    };
    const auto failCapacity = [] {
        return Core::failure(RenderErrorCode::PostProcessCapacityExceeded,
                             "Render pipeline schedule exceeded its fixed pass capacity");
    };

    for (u32 index = 0; index < chain.offscreenPasses.size(); ++index)
    {
        const auto& pass = chain.offscreenPasses[index];
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::OffscreenScene,
                .destinationBindingKey = pass.colorTargetBindingKey,
                .auxiliaryBindingKey = pass.depthTargetBindingKey,
                .destinationMipLevel = pass.colorMipLevel,
                .auxiliaryMipLevel = pass.depthMipLevel,
                .itemIndex = index,
                .clearColor = pass.clearColor,
                .clearDepth = pass.clearDepth,
            }))
        {
            return failCapacity();
        }
    }

    u32 current = chain.sceneColorTargetBindingKey;
    u8 currentMip = 0;
    const u32 decalBegin = schedule.m_passCount;
    for (u32 index = 0; index < chain.decals.size(); ++index)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::Decal,
                .sourceBindingKey = chain.sceneDepthTargetBindingKey,
                .destinationBindingKey = current,
                .itemIndex = index,
            }))
        {
            return failCapacity();
        }
    }
    std::sort(schedule.m_passes.begin() + decalBegin,
              schedule.m_passes.begin() + schedule.m_passCount,
              [&chain](const RenderPipelinePassPlan& left, const RenderPipelinePassPlan& right) {
                  const i32 leftOrder = chain.decals[left.itemIndex].order;
                  const i32 rightOrder = chain.decals[right.itemIndex].order;
                  return leftOrder != rightOrder ? leftOrder < rightOrder : left.itemIndex < right.itemIndex;
              });
    if (chain.fog.enabled &&
        !append(RenderPipelinePassPlan{
            .kind = RenderPipelinePassKind::Fog,
            .sourceBindingKey = chain.sceneDepthTargetBindingKey,
            .destinationBindingKey = current,
        }))
    {
        return failCapacity();
    }
    if (chain.bloom.enabled)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::BloomPrefilter,
                .sourceBindingKey = current,
                .destinationBindingKey = chain.bloomDownsampleTargetBindingKey,
            }))
        {
            return failCapacity();
        }
        for (u8 level = 1; level < chain.bloom.mipCount; ++level)
        {
            if (!append(RenderPipelinePassPlan{
                    .kind = RenderPipelinePassKind::BloomDownsample,
                    .sourceBindingKey = chain.bloomDownsampleTargetBindingKey,
                    .destinationBindingKey = chain.bloomDownsampleTargetBindingKey,
                    .sourceMipLevel = static_cast<u8>(level - 1U),
                    .destinationMipLevel = level,
                }))
            {
                return failCapacity();
            }
        }
        const u8 smallestLevel = static_cast<u8>(chain.bloom.mipCount - 1U);
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::BloomBlur,
                .sourceBindingKey = chain.bloomDownsampleTargetBindingKey,
                .destinationBindingKey = chain.bloomUpsampleTargetBindingKey,
                .sourceMipLevel = smallestLevel,
                .destinationMipLevel = smallestLevel,
            }))
        {
            return failCapacity();
        }
        for (u8 level = smallestLevel; level > 0; --level)
        {
            const u8 destinationLevel = static_cast<u8>(level - 1U);
            if (!append(RenderPipelinePassPlan{
                    .kind = RenderPipelinePassKind::BloomUpsample,
                    .sourceBindingKey = chain.bloomUpsampleTargetBindingKey,
                    .destinationBindingKey = chain.bloomUpsampleTargetBindingKey,
                    .auxiliaryBindingKey = chain.bloomDownsampleTargetBindingKey,
                    .sourceMipLevel = level,
                    .destinationMipLevel = destinationLevel,
                    .auxiliaryMipLevel = destinationLevel,
                }))
            {
                return failCapacity();
            }
        }
        // Additive RGB blend reads the attachment through the blend unit, never
        // through a sampler. Scene alpha and the original HDR scene are retained.
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::BloomComposite,
                .sourceBindingKey = chain.bloomUpsampleTargetBindingKey,
                .destinationBindingKey = current,
            }))
        {
            return failCapacity();
        }
    }

    for (u32 index = 0; index < chain.customSteps.size(); ++index)
    {
        const auto& step = chain.customSteps[index];
        if (!append(RenderPipelinePassPlan{
                .kind = step.kind == RenderPostProcessStepKind::Copy
                            ? RenderPipelinePassKind::Copy
                            : RenderPipelinePassKind::CustomShader,
                .sourceBindingKey = step.sourceBindingKey,
                .destinationBindingKey = step.destinationBindingKey,
                .sourceMipLevel = step.sourceMipLevel,
                .destinationMipLevel = step.destinationMipLevel,
                .shaderBindingKey = step.shaderBindingKey,
                .shaderUniformBindingKey = step.shaderUniformBindingKey,
                .itemIndex = index,
            }))
        {
            return failCapacity();
        }
        current = step.destinationBindingKey;
        currentMip = step.destinationMipLevel;
    }

    if (current != 0)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::ToneMapping,
                .sourceBindingKey = current,
                .destinationBindingKey = 0,
                .sourceMipLevel = currentMip,
            }))
        {
            return failCapacity();
        }
        current = 0;
    }
    if (hasPrimaryWindowUI && chain.enabled() &&
        !append(RenderPipelinePassPlan{
            .kind = RenderPipelinePassKind::UIComposite,
            .sourceBindingKey = current,
            .destinationBindingKey = 0,
        }))
    {
        return failCapacity();
    }
    return schedule;
}

LinearRgba toneMapLinearColor(LinearRgba color, const ToneMappingDesc& desc) noexcept
{
    const float exposure = std::isfinite(desc.exposure) && desc.exposure > 0.0F
                               ? desc.exposure
                               : 1.0F;
    const auto map = [&](float channel) {
        const float exposed = (std::min)(static_cast<double>(clampFiniteNonNegative(channel)) * exposure,
                                        65'504.0);
        const float mapped = std::clamp(toneMapChannel(static_cast<float>(exposed), desc.operation), 0.0F, 1.0F);
        return mapped <= 0.0031308F ? 12.92F * mapped
                                   : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
    };
    return LinearRgba{map(color.r), map(color.g), map(color.b),
                      std::clamp(std::isfinite(color.a) ? color.a : 1.0F, 0.0F, 1.0F)};
}

LinearRgba bloomPrefilterLinearColor(LinearRgba color, const BloomDesc& desc) noexcept
{
    const float brightness = (std::max)({clampFiniteNonNegative(color.r),
                                         clampFiniteNonNegative(color.g),
                                         clampFiniteNonNegative(color.b)});
    const float knee = (std::max)(desc.threshold * desc.softKnee, 1.0e-5F);
    float contribution = brightness - desc.threshold + knee;
    contribution = std::clamp(contribution, 0.0F, 2.0F * knee);
    contribution = contribution * contribution / (4.0F * knee + 1.0e-5F);
    contribution = (std::max)(contribution, brightness - desc.threshold) /
                   (std::max)(brightness, 1.0e-5F);
    // Intensity is applied once at final composite, not once per pyramid level.
    const float scale = contribution;
    return LinearRgba{clampFiniteNonNegative(color.r) * scale,
                      clampFiniteNonNegative(color.g) * scale,
                      clampFiniteNonNegative(color.b) * scale, 1.0F};
}

LinearRgba applyFogToLinearColor(LinearRgba color, float cameraDistance,
                                 const FogDesc& desc) noexcept
{
    if (!desc.enabled)
    {
        return color;
    }
    const float distance = clampFiniteNonNegative(cameraDistance);
    float visibility = 1.0F;
    switch (desc.mode)
    {
    case FogMode::Linear:
        visibility = (desc.linearEnd - distance) /
                     (std::max)(desc.linearEnd - desc.linearStart, 1.0e-5F);
        break;
    case FogMode::Exponential:
        visibility = std::exp(-desc.density * distance);
        break;
    case FogMode::ExponentialSquared:
    {
        const float factor = desc.density * distance;
        visibility = std::exp(-(factor * factor));
        break;
    }
    }
    visibility = std::clamp(visibility, 0.0F, 1.0F);
    const float fog = 1.0F - visibility;
    return LinearRgba{
        clampFiniteNonNegative(color.r) * visibility + desc.colorR * fog,
        clampFiniteNonNegative(color.g) * visibility + desc.colorG * fog,
        clampFiniteNonNegative(color.b) * visibility + desc.colorB * fog,
        std::clamp(std::isfinite(color.a) ? color.a : 1.0F, 0.0F, 1.0F),
    };
}

} // namespace Tina::Render
