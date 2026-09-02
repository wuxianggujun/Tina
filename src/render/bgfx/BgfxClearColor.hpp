#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderScene.hpp>

namespace Tina::Render::Bgfx {

// ADR 0042: the scene carries the clear colour in linear space, and the backbuffer is not
// an sRGB render target (no BGFX_RESET_SRGB_BACKBUFFER), so nothing encodes on our behalf.
// The opaque3D fragment shader ends with linearToSrgb() before writing, which means the
// framebuffer holds sRGB-encoded bytes; encoding here is what puts the background on the
// same scale as a lit surface instead of a stop darker than one.
//
// Split out of BgfxRenderDevice.cpp so the round trip against the sRGB transfer function
// is testable without a device: an off-by-one here shifts every background in the engine.
[[nodiscard]] u8 encodeSrgbComponent(float linear) noexcept;

// Packs to the 0xRRGGBBAA word bgfx::setViewClear expects.
[[nodiscard]] u32 packClearRgba(const RenderLinearColor& color) noexcept;

} // namespace Tina::Render::Bgfx
