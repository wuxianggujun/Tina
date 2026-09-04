#pragma once

#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx::ShaderDetail {

[[nodiscard]] Core::Result<bgfx::ProgramHandle> createSprite2DFixtureProgram();

// The engine vertex stage on its own, for linking against a custom fragment shader. Callers own the
// handle and must destroy it after every program built from it, since bgfx frees a shader once the
// last referencing program releases it.
[[nodiscard]] Core::Result<bgfx::ShaderHandle> createSprite2DVertexShader();

} // namespace Tina::Render::Bgfx::ShaderDetail
