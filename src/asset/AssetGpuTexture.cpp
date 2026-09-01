#include <tina/asset/AssetGpuTexture.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>

#include <array>
#include <span>

namespace Tina::Asset {
namespace {

// Asset owns the cooked-to-GPU vocabulary translation, because Render depends only
// on Core and must not name a cooked-asset type. The two enums are declared
// independently on purpose; these switches are the single place they are kept in
// step, and adding a value on either side fails here rather than silently mapping to
// something plausible.
[[nodiscard]] Core::Result<Render::GpuTextureFormat>
toGpuFormat(AssetFormat::Texture2DPixelFormat format) noexcept
{
    switch (format)
    {
    case AssetFormat::Texture2DPixelFormat::Rgba8Unorm:
        return Render::GpuTextureFormat::Rgba8Unorm;
    case AssetFormat::Texture2DPixelFormat::Bc1Rgba:
        return Render::GpuTextureFormat::Bc1Rgba;
    case AssetFormat::Texture2DPixelFormat::Bc3Rgba:
        return Render::GpuTextureFormat::Bc3Rgba;
    case AssetFormat::Texture2DPixelFormat::Bc7Rgba:
        return Render::GpuTextureFormat::Bc7Rgba;
    case AssetFormat::Texture2DPixelFormat::Astc4x4Rgba:
        return Render::GpuTextureFormat::Astc4x4Rgba;
    case AssetFormat::Texture2DPixelFormat::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked texture pixel format has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuTextureColorSpace>
toGpuColorSpace(AssetFormat::Texture2DColorSpace space) noexcept
{
    switch (space)
    {
    case AssetFormat::Texture2DColorSpace::Linear:
        return Render::GpuTextureColorSpace::Linear;
    case AssetFormat::Texture2DColorSpace::Srgb:
        return Render::GpuTextureColorSpace::Srgb;
    case AssetFormat::Texture2DColorSpace::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked texture color space has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuTextureWrapMode>
toGpuWrap(AssetFormat::Texture2DWrapMode mode) noexcept
{
    switch (mode)
    {
    case AssetFormat::Texture2DWrapMode::Repeat:
        return Render::GpuTextureWrapMode::Repeat;
    case AssetFormat::Texture2DWrapMode::Mirror:
        return Render::GpuTextureWrapMode::Mirror;
    case AssetFormat::Texture2DWrapMode::Clamp:
        return Render::GpuTextureWrapMode::Clamp;
    case AssetFormat::Texture2DWrapMode::Border:
        return Render::GpuTextureWrapMode::Border;
    case AssetFormat::Texture2DWrapMode::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked texture wrap mode has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuTextureFilterMode>
toGpuFilter(AssetFormat::Texture2DFilterMode mode) noexcept
{
    switch (mode)
    {
    case AssetFormat::Texture2DFilterMode::Point:
        return Render::GpuTextureFilterMode::Point;
    case AssetFormat::Texture2DFilterMode::Anisotropic:
        return Render::GpuTextureFilterMode::Anisotropic;
    case AssetFormat::Texture2DFilterMode::Linear:
        return Render::GpuTextureFilterMode::Linear;
    case AssetFormat::Texture2DFilterMode::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked texture filter mode has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuTextureMipFilterMode>
toGpuMipFilter(AssetFormat::Texture2DMipFilterMode mode) noexcept
{
    switch (mode)
    {
    case AssetFormat::Texture2DMipFilterMode::None:
        return Render::GpuTextureMipFilterMode::None;
    case AssetFormat::Texture2DMipFilterMode::Point:
        return Render::GpuTextureMipFilterMode::Point;
    case AssetFormat::Texture2DMipFilterMode::Linear:
        return Render::GpuTextureMipFilterMode::Linear;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked texture mip filter has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuTextureSamplerDesc>
toGpuSampler(const AssetFormat::Texture2DSamplerDesc& sampler) noexcept
{
    auto wrapU = toGpuWrap(sampler.wrapU);
    auto wrapV = toGpuWrap(sampler.wrapV);
    auto minFilter = toGpuFilter(sampler.minFilter);
    auto magFilter = toGpuFilter(sampler.magFilter);
    auto mipFilter = toGpuMipFilter(sampler.mipFilter);
    if (!wrapU)
    {
        return Core::failure(std::move(wrapU.error()));
    }
    if (!wrapV)
    {
        return Core::failure(std::move(wrapV.error()));
    }
    if (!minFilter)
    {
        return Core::failure(std::move(minFilter.error()));
    }
    if (!magFilter)
    {
        return Core::failure(std::move(magFilter.error()));
    }
    if (!mipFilter)
    {
        return Core::failure(std::move(mipFilter.error()));
    }
    return Render::GpuTextureSamplerDesc{
        .wrapU = *wrapU,
        .wrapV = *wrapV,
        .minFilter = *minFilter,
        .magFilter = *magFilter,
        .mipFilter = *mipFilter,
    };
}

} // namespace

Core::Result<Render::GpuTextureId> uploadTexture2DFromCooked(Render::IRenderDevice& device,
                                                             const CookedAssetFile& textureAsset)
{
    auto view = parseTexture2DFromCooked(textureAsset);
    if (!view)
    {
        return Core::failure(std::move(view.error()).withContext("uploadTexture2DFromCooked", "parse"));
    }
    auto format = toGpuFormat(view->pixelFormat);
    if (!format)
    {
        return Core::failure(std::move(format.error()));
    }
    auto colorSpace = toGpuColorSpace(view->colorSpace);
    if (!colorSpace)
    {
        return Core::failure(std::move(colorSpace.error()));
    }
    auto sampler = toGpuSampler(view->sampler);
    if (!sampler)
    {
        return Core::failure(std::move(sampler.error()));
    }

    // Fixed capacity matching the wire cap, so translating a full mip chain needs no
    // allocation on the load path.
    std::array<Render::Texture2DUploadLevel, AssetFormat::Texture2DWire::MaxLevelCount> uploadLevels{};
    const auto levels = view->levels();
    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        uploadLevels[index] = Render::Texture2DUploadLevel{
            .width = levels[index].width,
            .height = levels[index].height,
            .bytes = levels[index].bytes,
        };
    }

    return device.createTexture2D(Render::Texture2DUploadDesc{
        .format = *format,
        .colorSpace = *colorSpace,
        .sampler = *sampler,
        .levels = std::span<const Render::Texture2DUploadLevel>{uploadLevels}.first(levels.size()),
    });
}

Core::Status uploadAndBindTexture2DForSpriteKey(Render::IRenderDevice& device, const CookedAssetFile& textureAsset,
                                                Core::u32 spriteKey)
{
    if (spriteKey == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "spriteKey must be non-zero for texture binding");
    }
    auto texture = uploadTexture2DFromCooked(device, textureAsset);
    if (!texture)
    {
        return Core::failure(std::move(texture.error()));
    }
    if (auto status = device.setTexture2DBinding(spriteKey, *texture); !status)
    {
        (void)device.destroyTexture2D(*texture);
        return status;
    }
    return Core::success();
}

} // namespace Tina::Asset
