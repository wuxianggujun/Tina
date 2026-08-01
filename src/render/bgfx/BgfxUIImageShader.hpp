#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// RGBA image program. This is intentionally separate from the R8 coverage
// program used by solid and glyph UI commands.
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createUIImageQuadProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
