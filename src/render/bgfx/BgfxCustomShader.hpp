#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <bgfx/bgfx.h>

#include <algorithm>
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

struct CustomShaderTexture final {
    bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
    std::array<char, GpuShaderTextureValue::MaximumNameBytes + 1> name{};
    // Assigned here, at upload, rather than per draw: a stage is a hardware slot, and picking one per
    // draw would mean the engine's own fixed assignments below could collide with an author's.
    u8 stage = 0;
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
    // Samplers the author declared beyond the engine set, each already assigned the stage it will be
    // bound to for the life of the program.
    std::vector<CustomShaderTexture> authorTextures{};
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

// bgfx binds at most this many textures per draw (BGFX_CONFIG_MAX_TEXTURE_SAMPLERS). Every stage an
// author uses has to come out of what the engine set leaves free, which is why the author ceiling is
// per-kind rather than one number.
inline constexpr u8 MaximumTextureStageCount = 16;

// Stages the engine itself binds on each path, and therefore the first stage an author's sampler can
// occupy. Sprite2D binds s_tex and s_normalTex; Mesh3D binds base colour, metallic-roughness, normal,
// the CSM atlas, three IBL maps, the spot shadow map and six point-shadow faces.
//
// These are not derived from the setTexture call sites, so a new engine sampler must be added here
// too. Getting that wrong is not silent: an author texture would overwrite the engine's stage and the
// engine sampler would read the author's texture, which the assertion below cannot catch but any
// pixel criterion on the affected path will.
inline constexpr u8 Sprite2DEngineTextureStageCount = 2;
inline constexpr u8 Mesh3DEngineTextureStageCount = 14;

// What each kind can physically offer an author. Sprite2D is capped by the desc rather than by the
// hardware: 14 stages are free but a binding table carries 8, and advertising more than a caller can
// express would make the surplus unbindable.
static_assert(Sprite2DEngineTextureStageCount < MaximumTextureStageCount);
static_assert(Mesh3DEngineTextureStageCount < MaximumTextureStageCount);
inline constexpr u8 Sprite2DMaximumAuthorTextureCount =
    (std::min)(static_cast<u8>(MaximumTextureStageCount - Sprite2DEngineTextureStageCount),
               GpuShaderTextureBindingDesc::MaximumValueCount);
inline constexpr u8 Mesh3DMaximumAuthorTextureCount =
    (std::min)(static_cast<u8>(MaximumTextureStageCount - Mesh3DEngineTextureStageCount),
               GpuShaderTextureBindingDesc::MaximumValueCount);

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
//
// firstAuthorTextureStage and maximumAuthorTextures come from the caller because they are properties
// of the engine program being linked against, not of the author's binary: pass the pair belonging to
// desc.shaderKind. Author samplers are assigned consecutive stages from the first, in reflection
// order, and the count is enforced here so a sampler that could never be bound is refused at upload
// instead of silently reading whatever the engine left in that stage.
[[nodiscard]] Core::Result<CustomShaderProgram>
createCustomFragmentProgram(bgfx::ShaderHandle vertexShader, bgfx::ShaderHandle skinnedVertexShader,
                            std::span<const std::byte> fragmentBinary,
                            std::span<const bgfx::UniformHandle> engineUniforms,
                            u8 firstAuthorTextureStage, u8 maximumAuthorTextures);

} // namespace Tina::Render::Bgfx::ShaderDetail
