#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

// Translates a backend-neutral format to its bgfx token. Returns TextureFormat::Count
// for Invalid, which no adapter accepts, so an unmapped format fails the validity probe
// instead of silently uploading as some other format.
[[nodiscard]] bgfx::TextureFormat::Enum toBgfxTextureFormat(GpuTextureFormat format) noexcept;

// Composes the sampler and colour-space bits for one upload. Repeat wrap and Linear
// filtering are bgfx defaults and contribute nothing.
[[nodiscard]] u64 toBgfxTexture2DFlags(const Texture2DUploadDesc& desc) noexcept;

// Validates, concatenates the levels into one blob in level order, and creates the
// texture. Owns the whole translation so the device method is left with slot
// bookkeeping only; the caller must have already checked device liveness.
[[nodiscard]] Core::Result<bgfx::TextureHandle> createTexture2DUpload(
    const Texture2DUploadDesc& desc);

} // namespace Tina::Render::Bgfx
