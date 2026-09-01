#include "BgfxTexture2DUpload.hpp"

#include <tina/render/RenderErrors.hpp>

#include <cstring>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] u64 toBgfxWrapFlags(GpuTextureWrapMode mode, bool isU) noexcept
{
    // bgfx encodes Repeat as the absence of a wrap bit, so Repeat contributes nothing.
    switch (mode)
    {
    case GpuTextureWrapMode::Mirror:
        return isU ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_V_MIRROR;
    case GpuTextureWrapMode::Clamp:
        return isU ? BGFX_SAMPLER_U_CLAMP : BGFX_SAMPLER_V_CLAMP;
    case GpuTextureWrapMode::Border:
        return isU ? BGFX_SAMPLER_U_BORDER : BGFX_SAMPLER_V_BORDER;
    case GpuTextureWrapMode::Repeat:
    case GpuTextureWrapMode::Invalid:
        break;
    }
    return 0;
}

} // namespace

bgfx::TextureFormat::Enum toBgfxTextureFormat(GpuTextureFormat format) noexcept
{
    switch (format)
    {
    case GpuTextureFormat::Rgba8Unorm:
        return bgfx::TextureFormat::RGBA8;
    case GpuTextureFormat::Bc1Rgba:
        return bgfx::TextureFormat::BC1;
    case GpuTextureFormat::Bc3Rgba:
        return bgfx::TextureFormat::BC3;
    case GpuTextureFormat::Bc7Rgba:
        return bgfx::TextureFormat::BC7;
    case GpuTextureFormat::Astc4x4Rgba:
        return bgfx::TextureFormat::ASTC4x4;
    case GpuTextureFormat::Invalid:
        break;
    }
    return bgfx::TextureFormat::Count;
}

u64 toBgfxTexture2DFlags(const Texture2DUploadDesc& desc) noexcept
{
    u64 flags = BGFX_TEXTURE_NONE;
    // sRGB is a sampler-side decode flag in bgfx rather than a distinct format, so the
    // colour space the payload authored has to be applied here or the texture is read
    // as linear and gamma is applied twice.
    if (desc.colorSpace == GpuTextureColorSpace::Srgb)
    {
        flags |= BGFX_TEXTURE_SRGB;
    }
    flags |= toBgfxWrapFlags(desc.sampler.wrapU, true);
    flags |= toBgfxWrapFlags(desc.sampler.wrapV, false);
    // Linear is bgfx's default for each of the three filter stages, so only the
    // non-default choices contribute bits.
    if (desc.sampler.minFilter == GpuTextureFilterMode::Point)
    {
        flags |= BGFX_SAMPLER_MIN_POINT;
    } else if (desc.sampler.minFilter == GpuTextureFilterMode::Anisotropic)
    {
        flags |= BGFX_SAMPLER_MIN_ANISOTROPIC;
    }
    if (desc.sampler.magFilter == GpuTextureFilterMode::Point)
    {
        flags |= BGFX_SAMPLER_MAG_POINT;
    } else if (desc.sampler.magFilter == GpuTextureFilterMode::Anisotropic)
    {
        flags |= BGFX_SAMPLER_MAG_ANISOTROPIC;
    }
    if (desc.sampler.mipFilter == GpuTextureMipFilterMode::Point)
    {
        flags |= BGFX_SAMPLER_MIP_POINT;
    }
    // bgfx exposes anisotropy as a sampler enable plus a device-wide maximum. Device
    // creation enables that maximum; the public descriptor intentionally exposes no
    // per-texture numeric limit that this backend could not honour.
    return flags;
}

Core::Result<bgfx::TextureHandle> createTexture2DUpload(const Texture2DUploadDesc& desc)
{
    // Shared with the Null device so the two cannot drift on what they accept.
    if (auto status = validateTexture2DUploadDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    const bgfx::TextureFormat::Enum format = toBgfxTextureFormat(desc.format);
    const u64 flags = toBgfxTexture2DFlags(desc);
    const bool hasMips = desc.levels.size() > 1U;
    // Ask bgfx whether the running renderer supports the format before creating: a
    // compressed format missing on this adapter otherwise surfaces as an invalid handle
    // with no indication that the format, not the pixels, was the problem. The second
    // argument is bgfx's cube-map flag, not a mip flag, so it stays false for a 2D probe.
    if (!bgfx::isTextureValid(0, false, 1, format, flags))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "The active bgfx renderer does not support this Texture2D format");
    }

    // bgfx takes the whole chain as one blob in level order, which is exactly how the
    // cooked payload stores it, so the levels are concatenated rather than uploaded per
    // level.
    usize totalBytes = 0;
    for (const Texture2DUploadLevel& level : desc.levels)
    {
        totalBytes += level.bytes.size();
    }
    const bgfx::Memory* memory = bgfx::alloc(static_cast<u32>(totalBytes));
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload,
                             "bgfx could not allocate Texture2D upload memory");
    }
    usize writeOffset = 0;
    for (const Texture2DUploadLevel& level : desc.levels)
    {
        if (!level.bytes.empty())
        {
            std::memcpy(memory->data + writeOffset, level.bytes.data(), level.bytes.size());
        }
        writeOffset += level.bytes.size();
    }

    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        desc.baseWidth(), desc.baseHeight(), hasMips, 1, format, flags, memory);
    if (!bgfx::isValid(handle))
    {
        return Core::failure(RenderErrorCode::InvalidTextureUpload, "bgfx rejected Texture2D create");
    }
    return handle;
}

} // namespace Tina::Render::Bgfx
