#include <tina/render/RenderDevice.hpp>

#include <limits>

namespace Tina::Render {
namespace {

[[nodiscard]] bool isKnownFormat(GpuTextureFormat format) noexcept
{
    switch (format)
    {
    case GpuTextureFormat::Rgba8Unorm:
    case GpuTextureFormat::Bc1Rgba:
    case GpuTextureFormat::Bc3Rgba:
    case GpuTextureFormat::Bc7Rgba:
    case GpuTextureFormat::Astc4x4Rgba:
        return true;
    case GpuTextureFormat::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isBlockCompressed(GpuTextureFormat format) noexcept
{
    switch (format)
    {
    case GpuTextureFormat::Bc1Rgba:
    case GpuTextureFormat::Bc3Rgba:
    case GpuTextureFormat::Bc7Rgba:
    case GpuTextureFormat::Astc4x4Rgba:
        return true;
    case GpuTextureFormat::Rgba8Unorm:
    case GpuTextureFormat::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownColorSpace(GpuTextureColorSpace space) noexcept
{
    return space == GpuTextureColorSpace::Linear || space == GpuTextureColorSpace::Srgb;
}

[[nodiscard]] bool isKnownWrapMode(GpuTextureWrapMode mode) noexcept
{
    switch (mode)
    {
    case GpuTextureWrapMode::Repeat:
    case GpuTextureWrapMode::Mirror:
    case GpuTextureWrapMode::Clamp:
    case GpuTextureWrapMode::Border:
        return true;
    case GpuTextureWrapMode::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownFilterMode(GpuTextureFilterMode mode) noexcept
{
    switch (mode)
    {
    case GpuTextureFilterMode::Point:
    case GpuTextureFilterMode::Linear:
    case GpuTextureFilterMode::Anisotropic:
        return true;
    case GpuTextureFilterMode::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownMipFilterMode(GpuTextureMipFilterMode mode) noexcept
{
    switch (mode)
    {
    case GpuTextureMipFilterMode::None:
    case GpuTextureMipFilterMode::Point:
    case GpuTextureMipFilterMode::Linear:
        return true;
    }
    return false;
}

[[nodiscard]] u16 nextMipExtent(u16 extent) noexcept
{
    return extent > 1U ? static_cast<u16>(extent / 2U) : static_cast<u16>(1);
}

// Levels in a complete chain from the given base extent down to 1x1.
[[nodiscard]] u8 fullMipLevelCount(u16 width, u16 height) noexcept
{
    u8 count = 1;
    u16 levelWidth = width;
    u16 levelHeight = height;
    while (levelWidth > 1U || levelHeight > 1U)
    {
        levelWidth = nextMipExtent(levelWidth);
        levelHeight = nextMipExtent(levelHeight);
        ++count;
    }
    return count;
}

} // namespace

u32 gpuTextureLevelByteSize(GpuTextureFormat format, u16 width, u16 height) noexcept
{
    if (width == 0 || height == 0 || !isKnownFormat(format))
    {
        return 0;
    }
    constexpr u64 MaximumLevelBytes = (std::numeric_limits<u32>::max)();
    if (!isBlockCompressed(format))
    {
        const u64 size = static_cast<u64>(width) * static_cast<u64>(height) * 4U;
        return size > MaximumLevelBytes ? 0U : static_cast<u32>(size);
    }

    // A compressed level always costs whole 4x4 blocks, so a 1x1 tail level still
    // costs one full block. Rounding down would under-size the smallest mips.
    const u64 blocksX = (static_cast<u64>(width) + 3U) / 4U;
    const u64 blocksY = (static_cast<u64>(height) + 3U) / 4U;
    const u64 bytesPerBlock = format == GpuTextureFormat::Bc1Rgba ? 8U : 16U;
    const u64 size = blocksX * blocksY * bytesPerBlock;
    return size > MaximumLevelBytes ? 0U : static_cast<u32>(size);
}

Core::Status validateTexture2DUploadDesc(const Texture2DUploadDesc& desc) noexcept
{
    if (!isKnownFormat(desc.format))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D upload format is not a known value");
    }
    if (!isKnownColorSpace(desc.colorSpace))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D upload color space is not a known value");
    }
    if (!isKnownWrapMode(desc.sampler.wrapU) || !isKnownWrapMode(desc.sampler.wrapV))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D sampler wrap mode is not a known value");
    }
    if (!isKnownFilterMode(desc.sampler.minFilter) || !isKnownFilterMode(desc.sampler.magFilter) ||
        !isKnownMipFilterMode(desc.sampler.mipFilter))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D sampler filter mode is not a known value");
    }
    const bool anisotropicMin = desc.sampler.minFilter == GpuTextureFilterMode::Anisotropic;
    const bool anisotropicMag = desc.sampler.magFilter == GpuTextureFilterMode::Anisotropic;
    if (anisotropicMin != anisotropicMag)
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D anisotropic min and mag filters must be enabled together");
    }
    if (desc.levels.empty() || desc.levels.size() > Texture2DUploadDesc::MaximumLevelCount)
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D upload requires 1 to 15 mip levels");
    }
    if ((desc.levels.size() == 1U && desc.sampler.mipFilter != GpuTextureMipFilterMode::None) ||
        (desc.levels.size() > 1U && desc.sampler.mipFilter == GpuTextureMipFilterMode::None))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D mip filter must match whether a mip chain is present");
    }

    const Texture2DUploadLevel& base = desc.levels.front();
    if (base.width == 0 || base.height == 0 ||
        base.width > Texture2DUploadDesc::MaximumDimension ||
        base.height > Texture2DUploadDesc::MaximumDimension)
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D upload dimensions are out of range");
    }
    // A partial chain is rejected rather than padded: a backend told "5 levels" would
    // sample undefined bytes for the missing tail, and which levels are missing is
    // invisible from the pixels.
    if (desc.levels.size() > 1U &&
        desc.levels.size() != fullMipLevelCount(base.width, base.height))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "Texture2D mip chain must run from the base level down to 1x1");
    }

    u16 expectedWidth = base.width;
    u16 expectedHeight = base.height;
    for (const Texture2DUploadLevel& level : desc.levels)
    {
        if (level.width != expectedWidth || level.height != expectedHeight)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Texture2D mip level extent does not halve the previous level");
        }
        const u32 expectedSize = gpuTextureLevelByteSize(desc.format, level.width, level.height);
        if (expectedSize == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Texture2D mip level size overflowed");
        }
        if (level.bytes.size() != expectedSize)
        {
            return Core::failure(
                RenderErrorCode::InvalidTextureUpload,
                "Texture2D mip level byte count does not match its extent and format");
        }
        expectedWidth = nextMipExtent(expectedWidth);
        expectedHeight = nextMipExtent(expectedHeight);
    }
    return Core::success();
}

} // namespace Tina::Render
