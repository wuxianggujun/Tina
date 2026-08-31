#include <tina/render/RenderPostProcess.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Render {
namespace {

[[nodiscard]] constexpr bool isDepthFormat(RenderTextureFormat format) noexcept
{
    return format == RenderTextureFormat::Depth24Stencil8 ||
           format == RenderTextureFormat::Depth32Float;
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
    if (desc.format == RenderTextureFormat::Invalid || desc.usage == RenderTextureUsage::None)
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
    if ((chain.fog.enabled || chain.bloom.enabled ||
         chain.toneMapping.operation != ToneMappingOperator::None || !chain.decals.empty()) &&
        chain.sceneColorTargetBindingKey == 0)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Scene effects require a non-zero offscreen scene color target");
    }
    if (chain.bloom.enabled &&
        (chain.pingTargetBindingKey == 0 || chain.pongTargetBindingKey == 0 ||
         chain.pingTargetBindingKey == chain.pongTargetBindingKey))
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Bloom requires distinct ping and pong RenderTextures");
    }
    if (!std::isfinite(chain.toneMapping.exposure) || chain.toneMapping.exposure <= 0.0F ||
        !std::isfinite(chain.toneMapping.outputGamma) || chain.toneMapping.outputGamma <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                             "Tone mapping exposure and gamma must be positive and finite");
    }
    if (!std::isfinite(chain.bloom.threshold) || chain.bloom.threshold < 0.0F ||
        !finiteUnit(chain.bloom.softKnee) || !std::isfinite(chain.bloom.intensity) ||
        chain.bloom.intensity < 0.0F || chain.bloom.downsamplePassCount == 0 ||
        chain.bloom.downsamplePassCount > 10)
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
    for (const RenderOffscreenPassView& pass : chain.offscreenPasses)
    {
        if (pass.stablePassKey == 0 || pass.colorTargetBindingKey == 0 ||
            !finiteUnit(pass.clearR) || !finiteUnit(pass.clearG) ||
            !finiteUnit(pass.clearB) || !finiteUnit(pass.clearA))
        {
            return Core::failure(RenderErrorCode::InvalidOffscreenPass,
                                 "Offscreen pass identity, target, or clear color is invalid");
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
        if (step.sourceBindingKey == step.destinationBindingKey ||
            (step.kind == RenderPostProcessStepKind::CustomShader &&
             step.shaderBindingKey == 0))
        {
            return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                                 "Post-process step aliases its target or lacks a shader binding");
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
                .itemIndex = index,
                .clearColor = pass.clearColor,
                .clearDepth = pass.clearDepth,
            }))
        {
            return failCapacity();
        }
    }

    u32 current = chain.sceneColorTargetBindingKey;
    for (u32 index = 0; index < chain.decals.size(); ++index)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::Decal,
                .sourceBindingKey = current,
                .destinationBindingKey = current,
                .itemIndex = index,
            }))
        {
            return failCapacity();
        }
    }
    if (chain.fog.enabled &&
        !append(RenderPipelinePassPlan{
            .kind = RenderPipelinePassKind::Fog,
            .sourceBindingKey = current,
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
                .destinationBindingKey = chain.pingTargetBindingKey,
            }))
        {
            return failCapacity();
        }
        for (u32 iteration = 0; iteration < chain.bloom.downsamplePassCount; ++iteration)
        {
            if (!append(RenderPipelinePassPlan{
                    .kind = RenderPipelinePassKind::BloomDownsample,
                    .sourceBindingKey = (iteration & 1U) == 0U ? chain.pingTargetBindingKey
                                                               : chain.pongTargetBindingKey,
                    .destinationBindingKey = (iteration & 1U) == 0U ? chain.pongTargetBindingKey
                                                                    : chain.pingTargetBindingKey,
                    .iteration = iteration,
                }))
            {
                return failCapacity();
            }
        }
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::BloomBlur,
                .sourceBindingKey = chain.pingTargetBindingKey,
                .destinationBindingKey = chain.pongTargetBindingKey,
            }) ||
            !append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::BloomUpsample,
                .sourceBindingKey = chain.pongTargetBindingKey,
                .destinationBindingKey = current,
                .auxiliaryBindingKey = current,
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
                .auxiliaryBindingKey = step.shaderBindingKey,
                .itemIndex = index,
            }))
        {
            return failCapacity();
        }
        current = step.destinationBindingKey;
    }

    if (chain.toneMapping.operation != ToneMappingOperator::None)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::ToneMapping,
                .sourceBindingKey = current,
                .destinationBindingKey = 0,
            }))
        {
            return failCapacity();
        }
        current = 0;
    } else if (current != 0)
    {
        if (!append(RenderPipelinePassPlan{
                .kind = RenderPipelinePassKind::Copy,
                .sourceBindingKey = current,
                .destinationBindingKey = 0,
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
    const float gamma = std::isfinite(desc.outputGamma) && desc.outputGamma > 0.0F
                            ? desc.outputGamma
                            : 1.0F;
    const float inverseGamma = 1.0F / gamma;
    const auto map = [&](float channel) {
        const float mapped = toneMapChannel(channel * exposure, desc.operation);
        return std::pow(clampFiniteNonNegative(mapped), inverseGamma);
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
    const float scale = contribution * (std::max)(desc.intensity, 0.0F);
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
