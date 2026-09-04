#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Render::Bgfx::ShaderDetail {

// Picks the binary matching the live renderer and links it against an engine-owned vertex stage.
//
// Profile selection lives here rather than in the caller because only a live bgfx instance knows
// getRendererType(). A cooked shader carries every profile the host could produce, and exactly one
// of them is usable per run.

// Empty when the upload carries no binary for the active renderer, which is a fail-closed outcome
// rather than a reason to fall back: a fallback would draw the engine shader while reporting success.
[[nodiscard]] std::span<const std::byte>
selectCustomShaderBinary(const GpuShaderUploadDesc& desc) noexcept;

struct CustomShaderUniform final {
    bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
    // Copied out of bgfx::UniformInfo because that is only readable while the shader is alive, and
    // the name is what a caller's value table is keyed by.
    std::array<char, GpuShaderUniformValue::MaximumNameBytes + 1> name{};
};

struct CustomShaderProgram final {
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    // The same fragment binary linked against the skinned vertex stage, or invalid when the caller
    // passed no skinned stage. Both are built here rather than on first use because reflection is
    // only possible while the fragment handle is alive, and because a draw has no way to report the
    // failure of a lazily-built program.
    bgfx::ProgramHandle skinnedProgram = BGFX_INVALID_HANDLE;
    // Uniforms the author declared beyond the engine set, in the cooked table's order. Reflection
    // must happen while the fragment shader handle is still alive, so it is collected here rather
    // than left to the caller.
    std::vector<CustomShaderUniform> authorUniforms{};
};

// More than this many reflected uniforms is rejected rather than truncated: a truncated table would
// silently drop the author's last uniforms and report success.
//
// This bounds the *reflected* table, which includes the engine set the contract .sh declares -- 7 for
// Sprite2D but 32 for Mesh3D -- so it cannot be lowered to the author budget below. The author count
// is checked separately after the engine handles are subtracted.
inline constexpr u16 MaximumReflectedUniformCount = 64;

// An author may declare no more uniforms than one value binding can carry. Without this, uniforms
// past the 16th reflect and link fine and then get published as zero on every batch forever, because
// GpuShaderUniformBindingDesc cannot express a value for them -- success at upload, silently wrong
// pixels at every draw. Rejecting at upload is what makes the advertised limit usable.
static_assert(GpuShaderUniformBindingDesc::MaximumValueCount <= MaximumReflectedUniformCount);
inline constexpr u16 MaximumAuthorUniformCount = GpuShaderUniformBindingDesc::MaximumValueCount;

// Consumes no shader handle: vertexShader and skinnedVertexShader stay owned by the caller, and the
// returned programs own only the fragment shader created internally. bgfx destroys a program's
// shaders when the last referencing program is destroyed, so the engine vertex shaders must outlive
// every program built from them.
//
// skinnedVertexShader may be invalid, which yields an invalid CustomShaderProgram::skinnedProgram.
// When it is valid, both programs are built from one fragment binary and one reflection pass, so the
// author uniform handles are shared: bgfx dedupes uniforms by name globally, which makes a single
// handle the identity of a name across both programs.
//
// engineUniforms are the handles the device created for the contract .sh set. They are subtracted by
// handle identity rather than by name because bgfx dedupes uniforms by name hash globally, which
// makes the handle the exact identity of a name.
[[nodiscard]] Core::Result<CustomShaderProgram>
createCustomFragmentProgram(bgfx::ShaderHandle vertexShader, bgfx::ShaderHandle skinnedVertexShader,
                            std::span<const std::byte> fragmentBinary,
                            std::span<const bgfx::UniformHandle> engineUniforms);

} // namespace Tina::Render::Bgfx::ShaderDetail
