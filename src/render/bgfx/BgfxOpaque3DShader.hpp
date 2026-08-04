#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// Product Opaque3D path: experimental metallic-roughness hybrid (RENDER-001).
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DMrProgram();

// Position-only instanced depth pass for the fixed directional shadow map.
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DShadowProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
