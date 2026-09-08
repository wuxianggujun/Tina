#pragma once

#include <tina/render/RenderDevice.hpp>

#include <algorithm>
#include <optional>

namespace Tina::Render::Detail {

struct RenderTextureResourceView final {
    GpuRenderTextureId identity{};
    RenderTextureDesc desc{};
};

[[nodiscard]] constexpr RenderSurfaceExtent renderTextureMipExtent(const RenderTextureDesc& desc,
                                                                  u8 level) noexcept
{
    return {static_cast<u32>((std::max)(1U, static_cast<u32>(desc.width) >> level)),
            static_cast<u32>((std::max)(1U, static_cast<u32>(desc.height) >> level))};
}

[[nodiscard]] constexpr bool isDepthRenderTextureFormat(RenderTextureFormat format) noexcept
{
    return format == RenderTextureFormat::Depth24Stencil8 || format == RenderTextureFormat::Depth32Float;
}

[[nodiscard]] inline bool sceneNeedsDepth(RenderSceneView scene) noexcept
{
    return !scene.opaqueMeshes3D().empty() || !scene.opaqueSkinnedMeshes3D().empty() ||
           !scene.transparent3DDraws().empty();
}

// All backends use the same role/extent/subresource/alias checks. Resolve verifies
// the receiving device's owner, live generation and binding before returning a
// descriptor. No backend handle or retained caller view crosses this seam.
template <typename Resolve>
[[nodiscard]] Core::Status validatePostProcessResources(const RenderPostProcessChainView& chain,
                                                       RenderSceneView primaryScene,
                                                       Resolve&& resolve) noexcept
{
    if (auto status = validateRenderPostProcessChain(chain); !status) return status;
    if (!chain.enabled()) return Core::success();

    const auto require = [&](u32 key, u8 mip, bool depth, bool sampled, bool hdr) -> Core::Status {
        const auto resource = resolve(key);
        if (!resource)
        {
            Core::Error error{RenderErrorCode::RenderTextureNotFound, "Post-process render texture binding is not live"};
            error.addContext("bindingKey", std::to_string(key));
            return Core::failure(std::move(error));
        }
        const RenderTextureDesc& desc = resource->desc;
        const auto attachment = depth ? RenderTextureUsage::DepthStencilAttachment : RenderTextureUsage::ColorAttachment;
        if (mip >= desc.mipCount || depth != isDepthRenderTextureFormat(desc.format) ||
            !hasRenderTextureUsage(desc.usage, attachment) ||
            (sampled && (!hasRenderTextureUsage(desc.usage, RenderTextureUsage::Sampled) || desc.sampleCount != 1)) ||
            (hdr && desc.format != RenderTextureFormat::Rgba16Float))
        {
            return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                                 "Render texture format, usage, mip or sample count does not satisfy its pass role");
        }
        return Core::success();
    };
    const auto attachments = [&](u32 colorKey, u8 colorMip, u32 depthKey, u8 depthMip,
                                 bool needsDepth, bool hdr) -> Core::Status {
        if (auto status = require(colorKey, colorMip, false, hdr, hdr); !status) return status;
        if (depthKey == 0)
        {
            return needsDepth ? Core::failure(RenderErrorCode::InvalidOffscreenPass,
                                              "A 3D offscreen scene requires a depth attachment") : Core::success();
        }
        if (auto status = require(depthKey, depthMip, true, false, false); !status) return status;
        const auto color = *resolve(colorKey);
        const auto depth = *resolve(depthKey);
        const auto colorExtent = renderTextureMipExtent(color.desc, colorMip);
        const auto depthExtent = renderTextureMipExtent(depth.desc, depthMip);
        if (color.identity == depth.identity || colorExtent.width != depthExtent.width ||
            colorExtent.height != depthExtent.height || color.desc.sampleCount != depth.desc.sampleCount)
        {
            return Core::failure(RenderErrorCode::InvalidOffscreenPass,
                                 "Offscreen attachments must have distinct identities and matching extents/sample counts");
        }
        return Core::success();
    };

    if (chain.sceneColorTargetBindingKey != 0)
    {
        if (auto status = attachments(chain.sceneColorTargetBindingKey, 0, chain.sceneDepthTargetBindingKey, 0,
                                      sceneNeedsDepth(primaryScene), true); !status) return status;
    }
    for (const auto& pass : chain.offscreenPasses)
    {
        if (auto status = attachments(pass.colorTargetBindingKey, pass.colorMipLevel, pass.depthTargetBindingKey,
                                      pass.depthMipLevel, sceneNeedsDepth(pass.scene), false); !status) return status;
    }
    if (chain.fog.enabled || !chain.decals.empty())
    {
        if (!primaryScene.perspectiveCamera())
            return Core::failure(RenderErrorCode::InvalidPostProcessChain, "Depth effects require a perspective camera");
        if (auto status = require(chain.sceneDepthTargetBindingKey, 0, true, true, false); !status) return status;
    }
    if (chain.bloom.enabled)
    {
        const u8 lastMip = static_cast<u8>(chain.bloom.mipCount - 1U);
        for (u32 key : {chain.bloomDownsampleTargetBindingKey, chain.bloomUpsampleTargetBindingKey})
        {
            if (auto status = require(key, lastMip, false, true, true); !status) return status;
            const auto scene = *resolve(chain.sceneColorTargetBindingKey);
            const auto bloom = *resolve(key);
            if (scene.identity == bloom.identity ||
                bloom.desc.width != (std::max)(1U, static_cast<u32>(scene.desc.width) / 2U) ||
                bloom.desc.height != (std::max)(1U, static_cast<u32>(scene.desc.height) / 2U))
            {
                return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                                     "Bloom pyramids must be distinct half-resolution RGBA16F scene targets");
            }
        }
        if (resolve(chain.bloomDownsampleTargetBindingKey)->identity ==
            resolve(chain.bloomUpsampleTargetBindingKey)->identity)
            return Core::failure(RenderErrorCode::InvalidPostProcessChain, "Bloom pyramids alias the same texture");
    }
    for (const auto& step : chain.customSteps)
    {
        if (auto status = require(step.sourceBindingKey, step.sourceMipLevel, false, true, false); !status) return status;
        if (auto status = require(step.destinationBindingKey, step.destinationMipLevel, false, true, false); !status) return status;
        const auto source = *resolve(step.sourceBindingKey);
        const auto destination = *resolve(step.destinationBindingKey);
        if ((source.identity == destination.identity && step.sourceMipLevel == step.destinationMipLevel) ||
            source.desc.format == RenderTextureFormat::Rgba8Srgb || destination.desc.format == RenderTextureFormat::Rgba8Srgb)
            return Core::failure(RenderErrorCode::InvalidPostProcessChain,
                                 "Custom/copy steps require non-aliasing linear subresources");
    }
    return Core::success();
}

} // namespace Tina::Render::Detail
