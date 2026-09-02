#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

// The decode destination format. bgfx converts the decoder's native YUV output into
// this before the texture becomes sampleable, so a caller never sees a planar
// surface and needs no colour-conversion shader.
inline constexpr bgfx::TextureFormat::Enum VideoDecodeDestinationFormat =
    bgfx::TextureFormat::BGRA8;

[[nodiscard]] bgfx::VideoCodec::Enum toBgfxVideoCodec(VideoCodec codec) noexcept;

// bgfx encodes chroma as 0 = 4:2:0, 2 = 4:2:2, 4 = 4:4:4 (see bgfx::isVideoCodecValid).
// The values are not consecutive, so this cannot be a cast.
[[nodiscard]] u8 toBgfxChromaCode(VideoChromaSubsampling chroma) noexcept;

// Reads the live device's video capabilities. Reports an all-false set when the
// renderer has no decoder compiled in, which is every OpenGL/OpenGLES build.
[[nodiscard]] VideoDecodeCapabilities readVideoDecodeCapabilities() noexcept;

// Whether this exact stream shape is decodable, combining the coarse capability
// bits with bgfx's per-stream probe. Both are required: the capability bits do not
// account for extent or picture-buffer limits.
[[nodiscard]] bool isVideoDecodeSupported(const VideoDecodeTextureDesc& desc) noexcept;

// Creates the decode destination. bgfx infers "this texture is a video decode
// target" from the VideoDecoderInit magic leading the creation blob rather than
// from a texture flag, so the parameter sets travel with the creation call.
[[nodiscard]] Core::Result<bgfx::TextureHandle> createVideoDecodeTexture(
    const VideoDecodeTextureDesc& desc);

// Submits one decode/presentation step. The submission is serialised into the
// memory blob handed to bgfx::updateTexture2D, which is how bgfx receives decode
// work for a target created above.
[[nodiscard]] Core::Status submitVideoDecodeFrame(bgfx::TextureHandle texture,
                                                  u16 codedWidth,
                                                  u16 codedHeight,
                                                  const VideoDecodeSubmission& submission);

} // namespace Tina::Render::Bgfx
