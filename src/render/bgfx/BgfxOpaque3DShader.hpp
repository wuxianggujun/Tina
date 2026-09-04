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

// Vertex shader handles for custom Mesh3D fragment programs. Neither transfers ownership; both are
// kept alive for the device's whole lifetime.
//
// There are two because rigid and skinned geometry need different vertex stages while sharing one
// fragment stage -- exactly as the engine's own createOpaque3DMrProgram and
// createOpaque3DSkinnedMrProgram both link fs_tina_opaque3d_mr. Both .def.sc files declare the same
// five varyings, so their output hashes match and a single cooked Mesh3D fragment binary links
// against either. That is what keeps skinning out of the authoring contract: an author cooks one
// Mesh3D shader, never a skinned variant of it.
[[nodiscard]] Core::Result<bgfx::ShaderHandle> createOpaque3DMrVertexShader();
[[nodiscard]] Core::Result<bgfx::ShaderHandle> createOpaque3DSkinnedVertexShader();

} // namespace Tina::Render::Bgfx::ShaderDetail
