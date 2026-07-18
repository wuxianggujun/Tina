#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

[[nodiscard]] Core::Result<bgfx::ProgramHandle> createUISolidQuadProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
