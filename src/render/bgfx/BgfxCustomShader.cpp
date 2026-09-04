#include "BgfxCustomShader.hpp"

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace Tina::Render::Bgfx::ShaderDetail {
namespace {

[[nodiscard]] GpuShaderBinaryProfile activeRendererProfile() noexcept
{
    switch (bgfx::getRendererType())
    {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return GpuShaderBinaryProfile::Dxbc50;
    case bgfx::RendererType::OpenGL:
        return GpuShaderBinaryProfile::Glsl120;
    case bgfx::RendererType::OpenGLES:
        return GpuShaderBinaryProfile::Essl300;
    case bgfx::RendererType::Vulkan:
        return GpuShaderBinaryProfile::SpirV;
    default:
        break;
    }
    return GpuShaderBinaryProfile::Invalid;
}

} // namespace

std::span<const std::byte> selectCustomShaderBinary(const GpuShaderUploadDesc& desc) noexcept
{
    const GpuShaderBinaryProfile wanted = activeRendererProfile();
    if (wanted == GpuShaderBinaryProfile::Invalid)
    {
        return {};
    }
    for (const GpuShaderBinary& binary : desc.binaries)
    {
        if (binary.profile == wanted)
        {
            return binary.bytes;
        }
    }
    return {};
}

Core::Result<CustomShaderProgram>
createCustomFragmentProgram(bgfx::ShaderHandle vertexShader, bgfx::ShaderHandle skinnedVertexShader,
                            std::span<const std::byte> fragmentBinary,
                            std::span<const bgfx::UniformHandle> engineUniforms)
{
    if (!bgfx::isValid(vertexShader) || fragmentBinary.empty())
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "A custom shader program needs a valid engine vertex stage and a binary");
    }
    if (fragmentBinary.size() > static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Custom shader binary exceeds what bgfx can reference");
    }
    // Copied rather than referenced: the caller's span borrows cooked asset bytes that may be
    // released as soon as the upload returns, while bgfx reads the memory on the render thread.
    const bgfx::Memory* memory =
        bgfx::copy(fragmentBinary.data(), static_cast<Core::u32>(fragmentBinary.size()));
    const bgfx::ShaderHandle fragmentShader = bgfx::createShader(memory);
    if (!bgfx::isValid(fragmentShader))
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "bgfx rejected the custom fragment shader binary");
    }

    // Reflected before the handle is released below: getShaderUniforms reads the ShaderRef, which
    // only exists while the shader is alive. bgfx already excludes its own predefined uniforms
    // (u_modelViewProj and friends) from this table, so only declared ones appear.
    std::array<bgfx::UniformHandle, MaximumReflectedUniformCount> reflected{};
    const u16 reflectedCount =
        bgfx::getShaderUniforms(fragmentShader, reflected.data(), MaximumReflectedUniformCount);
    if (reflectedCount > MaximumReflectedUniformCount)
    {
        bgfx::destroy(fragmentShader);
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "The custom fragment shader declares more uniforms than the device can "
                             "reflect, which would silently truncate the author's table");
    }

    CustomShaderProgram result{};
    for (u16 index = 0; index < reflectedCount; ++index)
    {
        const bgfx::UniformHandle candidate = reflected[index];
        const bool isEngineUniform =
            std::find_if(engineUniforms.begin(), engineUniforms.end(),
                         [candidate](bgfx::UniformHandle engine) noexcept {
                             return engine.idx == candidate.idx;
                         }) != engineUniforms.end();
        if (isEngineUniform)
        {
            continue;
        }

        bgfx::UniformInfo info{};
        bgfx::getUniformInfo(candidate, info);
        if (info.type != bgfx::UniformType::Vec4 || info.num != 1)
        {
            bgfx::destroy(fragmentShader);
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A custom fragment shader may only declare scalar vec4 uniforms "
                                 "beyond the engine set");
        }
        const usize nameLength = std::char_traits<char>::length(info.name);
        if (nameLength == 0 || nameLength > GpuShaderUniformValue::MaximumNameBytes)
        {
            bgfx::destroy(fragmentShader);
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A custom fragment shader uniform name exceeds what a value binding "
                                 "can address");
        }
        if (result.authorUniforms.size() >= MaximumAuthorUniformCount)
        {
            bgfx::destroy(fragmentShader);
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A custom fragment shader declares more author uniforms than one "
                                 "value binding can carry, so the surplus could only ever publish zero");
        }

        CustomShaderUniform entry{};
        entry.handle = candidate;
        std::copy_n(info.name, nameLength, entry.name.begin());
        result.authorUniforms.push_back(entry);
    }

    // destroyShaders=false because both vertex stages are shared by every program of this kind.
    // bgfx refcounts each stage, so a program holds its own reference and the fragment handle can be
    // released once the last program has been built from it.
    const bgfx::ProgramHandle program = bgfx::createProgram(vertexShader, fragmentShader, false);
    if (!bgfx::isValid(program))
    {
        bgfx::destroy(fragmentShader);
        // The likely cause is a varying mismatch: bgfx compares the vertex stage's output hash with
        // the fragment stage's input hash and refuses to link when they differ. That is what stops a
        // Mesh3D fragment binary from being bound to a Sprite2D draw, so it is a contract violation
        // rather than an out-of-memory condition.
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "bgfx refused to link the custom fragment shader against the engine "
                             "vertex stage, which usually means the varying contract does not match "
                             "the shader kind");
    }

    bgfx::ProgramHandle skinnedProgram = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(skinnedVertexShader))
    {
        skinnedProgram = bgfx::createProgram(skinnedVertexShader, fragmentShader, false);
        if (!bgfx::isValid(skinnedProgram))
        {
            bgfx::destroy(program);
            bgfx::destroy(fragmentShader);
            // Both stages declare the same varyings, so a rigid link that succeeds and a skinned one
            // that fails is not an author mistake the cook step could have caught. Refusing the whole
            // upload keeps a skinned draw from silently falling back to the engine fragment stage.
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "bgfx linked the custom fragment shader against the rigid vertex "
                                 "stage but refused the skinned one, so a skinned draw could only "
                                 "ever run the engine fragment stage");
        }
    }
    bgfx::destroy(fragmentShader);
    result.program = program;
    result.skinnedProgram = skinnedProgram;
    return result;
}

} // namespace Tina::Render::Bgfx::ShaderDetail
