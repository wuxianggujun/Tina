#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

#include <span>

namespace Tina::Render::Bgfx {

// Creates a 1x1 R8 white texture for SolidQuad sampling (coverage = 1).
[[nodiscard]] Core::Result<bgfx::TextureHandle> createUISolidWhiteTexture();

// Creates an R8 atlas page texture. Pixels must be width*height bytes, row-major.
// Memory is copied by bgfx; the caller retains ownership of `pixels`.
[[nodiscard]] Core::Result<bgfx::TextureHandle> createUIGlyphAtlasTexture(
    u32 width,
    u32 height,
    std::span<const u8> pixels);

// Replaces full page contents. Size must match the texture create size.
[[nodiscard]] Core::Status updateUIGlyphAtlasTexture(
    bgfx::TextureHandle texture,
    u32 width,
    u32 height,
    std::span<const u8> pixels);

} // namespace Tina::Render::Bgfx
