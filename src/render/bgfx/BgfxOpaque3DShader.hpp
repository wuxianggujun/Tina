#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// Product Opaque3D path: experimental metallic-roughness hybrid (RENDER-001).
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DMrProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
