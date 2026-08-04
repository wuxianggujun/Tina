#include "BgfxEnvironmentMapResources.hpp"

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

constexpr u64 EnvironmentCubeSamplerFlags =
    BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
    BGFX_SAMPLER_W_CLAMP;
constexpr u64 EnvironmentBrdfSamplerFlags =
    BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr usize CubeFaceCount = 6U;
constexpr usize Rgba16FloatBytesPerPixel = 8U;

} // namespace

Core::Result<BgfxEnvironmentMapResources>
createEnvironmentMapResources(const EnvironmentMapUploadDesc& desc)
{
    if (auto status = validateEnvironmentMapUploadDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    BgfxEnvironmentMapResources resources{};
    resources.diffuseIrradiance = bgfx::createTextureCube(
        desc.diffuseFaceSize, false, 1, bgfx::TextureFormat::RGBA16F,
        EnvironmentCubeSamplerFlags);
    if (!bgfx::isValid(resources.diffuseIrradiance))
    {
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "bgfx rejected the diffuse irradiance cubemap");
    }

    resources.prefilteredSpecular = bgfx::createTextureCube(
        desc.specularFaceSize, true, 1, bgfx::TextureFormat::RGBA16F,
        EnvironmentCubeSamplerFlags);
    if (!bgfx::isValid(resources.prefilteredSpecular))
    {
        destroyEnvironmentMapResources(resources);
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "bgfx rejected the prefiltered specular cubemap");
    }

    resources.brdfLut = bgfx::createTexture2D(
        desc.brdfWidth, desc.brdfHeight, false, 1, bgfx::TextureFormat::RG16F,
        EnvironmentBrdfSamplerFlags,
        bgfx::copy(desc.brdfRg16FloatPixels.data(),
                   static_cast<u32>(desc.brdfRg16FloatPixels.size())));
    if (!bgfx::isValid(resources.brdfLut))
    {
        destroyEnvironmentMapResources(resources);
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "bgfx rejected the BRDF integration LUT");
    }

    const usize diffuseFaceBytes = static_cast<usize>(desc.diffuseFaceSize) *
                                   desc.diffuseFaceSize * Rgba16FloatBytesPerPixel;
    for (u8 face = 0; face < CubeFaceCount; ++face)
    {
        const auto pixels = desc.diffuseRgba16FloatPixels.subspan(
            static_cast<usize>(face) * diffuseFaceBytes, diffuseFaceBytes);
        bgfx::updateTextureCube(
            resources.diffuseIrradiance, 0, face, 0, 0, 0,
            desc.diffuseFaceSize, desc.diffuseFaceSize,
            bgfx::copy(pixels.data(), static_cast<u32>(pixels.size())));
    }

    usize specularOffset = 0;
    u16 mipExtent = desc.specularFaceSize;
    for (u8 mip = 0; mip < desc.specularMipCount; ++mip)
    {
        const usize faceBytes = static_cast<usize>(mipExtent) * mipExtent *
                                Rgba16FloatBytesPerPixel;
        for (u8 face = 0; face < CubeFaceCount; ++face)
        {
            const auto pixels = desc.specularRgba16FloatPixels.subspan(
                specularOffset, faceBytes);
            bgfx::updateTextureCube(
                resources.prefilteredSpecular, 0, face, mip, 0, 0,
                mipExtent, mipExtent,
                bgfx::copy(pixels.data(), static_cast<u32>(pixels.size())));
            specularOffset += faceBytes;
        }
        mipExtent = static_cast<u16>((std::max)(1U, static_cast<u32>(mipExtent) / 2U));
    }

    return resources;
}

void destroyEnvironmentMapResources(BgfxEnvironmentMapResources& resources) noexcept
{
    if (bgfx::isValid(resources.brdfLut))
    {
        bgfx::destroy(resources.brdfLut);
        resources.brdfLut = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(resources.prefilteredSpecular))
    {
        bgfx::destroy(resources.prefilteredSpecular);
        resources.prefilteredSpecular = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(resources.diffuseIrradiance))
    {
        bgfx::destroy(resources.diffuseIrradiance);
        resources.diffuseIrradiance = BGFX_INVALID_HANDLE;
    }
}

} // namespace Tina::Render::Bgfx
