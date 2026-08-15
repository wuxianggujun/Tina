#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// Product Opaque3D path: experimental metallic-roughness hybrid (RENDER-001).
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DMrProgram();

// 3D-SKIN-001 A3: palette-skinned vertex stage paired with the shared
// fs_tina_opaque3d_mr fragment stage.
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createOpaque3DSkinnedMrProgram();

// Position-only instanced depth pass for one cascade tile in the fixed CSM atlas.
[[nodiscard]] Core::Result<bgfx::ProgramHandle>
createOpaque3DCascadedShadowDepthProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
