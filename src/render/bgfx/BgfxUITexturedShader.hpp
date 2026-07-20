#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// UI pass program that multiplies premultiplied vertex color by R8 coverage
// from s_texColor. Solid quads bind a 1x1 white page; Glyph quads bind an atlas.
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createUITexturedQuadProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
