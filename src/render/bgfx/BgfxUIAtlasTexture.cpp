#include "BgfxUIAtlasTexture.hpp"

#include <tina/render/RenderErrors.hpp>

#include <limits>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] Core::Status invalidAtlas(const char* message)
{
    return Core::failure(RenderErrorCode::InvalidDrawCommand, message);
}

} // namespace

Core::Result<bgfx::TextureHandle> createUISolidWhiteTexture()
{
    const u8 white = 255;
    const bgfx::Memory* memory = bgfx::copy(&white, 1);
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx failed to allocate the UI solid white texture memory");
    }
    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        1,
        1,
        false,
        1,
        bgfx::TextureFormat::R8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
        memory);
    if (!bgfx::isValid(texture))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the UI solid white texture");
    }
    return texture;
}

Core::Result<bgfx::TextureHandle> createUIGlyphAtlasTexture(
    u32 width,
    u32 height,
    std::span<const u8> pixels)
{
    if (width == 0 || height == 0)
    {
        return Core::failure(
            invalidAtlas("UI glyph atlas texture dimensions must be greater than zero").error());
    }
    const u64 required = static_cast<u64>(width) * static_cast<u64>(height);
    if (pixels.size() < required)
    {
        return Core::failure(
            invalidAtlas("UI glyph atlas pixel buffer is shorter than width*height").error());
    }
    if (required > static_cast<u64>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(invalidAtlas("UI glyph atlas pixel buffer is too large").error());
    }

    const bgfx::Memory* memory =
        bgfx::copy(pixels.data(), static_cast<u32>(required));
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx failed to allocate UI glyph atlas texture memory");
    }
    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::R8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
        memory);
    if (!bgfx::isValid(texture))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the UI glyph atlas texture");
    }
    return texture;
}

Core::Status updateUIGlyphAtlasTexture(
    bgfx::TextureHandle texture,
    u32 width,
    u32 height,
    std::span<const u8> pixels)
{
    if (!bgfx::isValid(texture))
    {
        return invalidAtlas("UI glyph atlas texture handle is invalid");
    }
    if (width == 0 || height == 0)
    {
        return invalidAtlas("UI glyph atlas texture dimensions must be greater than zero");
    }
    const u64 required = static_cast<u64>(width) * static_cast<u64>(height);
    if (pixels.size() < required)
    {
        return invalidAtlas("UI glyph atlas pixel buffer is shorter than width*height");
    }
    if (required > static_cast<u64>((std::numeric_limits<u32>::max)()))
    {
        return invalidAtlas("UI glyph atlas pixel buffer is too large");
    }

    const bgfx::Memory* memory =
        bgfx::copy(pixels.data(), static_cast<u32>(required));
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx failed to allocate UI glyph atlas update memory");
    }
    bgfx::updateTexture2D(
        texture,
        0,
        0,
        0,
        0,
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        memory);
    return Core::success();
}

} // namespace Tina::Render::Bgfx
