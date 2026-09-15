#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

// World-space billboard program for 3D particles. Corners arrive pre-expanded, so
// the vertex stage uses u_viewProj with no per-draw model transform. The fragment
// stage premultiplies its output, which is what lets one program serve both the
// alpha-blend and additive states.
[[nodiscard]] Core::Result<bgfx::ProgramHandle> createParticle3DProgram();

} // namespace Tina::Render::Bgfx::ShaderDetail
