#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DUnlitProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
