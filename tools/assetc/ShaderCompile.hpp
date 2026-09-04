#pragma once

#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::AssetC {

// Drives the bgfx shaderc as a child process to turn one custom fragment shader into a Shader
// payload file, which a recipe then cooks with the generic `asset Shader <id> <payload>` line.
//
// shaderc is invoked rather than linked on purpose: linking it would drag glslang, spirv-cross and
// the HLSL compiler into this tool's link line for a code path most cooks never take. The child's
// exit code and stderr are the whole failure surface, and both are reported verbatim, because a
// shader author needs shaderc's own line and column rather than a summary of it.
//
// Every path is supplied by the caller. Nothing here is resolved relative to a build tree: this
// tool is installed with the SDK, so a baked-in shaderc or varying-def path would work only in the
// tree it was compiled from.

struct ShaderCompileRequest final {
    // Host-executable shaderc, from --shaderc. There is deliberately no PATH fallback: a
    // silently-picked shaderc could target a different bgfx revision than the varying def below,
    // and the mismatch would surface as a wrong picture rather than a cook error.
    std::string shadercPath;
    std::string sourcePath;
    // The .def.sc pinning the varying contract for this kind. shaderc rejects an `$input` naming a
    // varying the def does not declare, so a def/kind mismatch fails the cook instead of linking
    // against varyings the source never declared.
    std::string varyingDefPath;
    // Written only after every profile compiled; a failure leaves the previous file untouched.
    std::string outputPath;
    AssetFormat::ShaderKind shaderKind = AssetFormat::ShaderKind::Invalid;
    // shaderc -i entries, in order. At least one is required: the contract header this shader must
    // include lives in one of them, as does bgfx_shader.sh.
    std::vector<std::string> includeDirs{};
    // Which renderer binaries to produce, from --shader-profiles. Empty means the host default set
    // (glsl120 + spv, plus dxbc on a Windows host), matching tina_bgfx_shader_profiles() with mobile
    // shaders off. Naming essl300 here is what a mobile cook needs: the engine's own shaders gain
    // that profile under TINA_RENDER_BGFX_MOBILE_SHADERS, and a custom shader without it cannot bind
    // on the GLES renderer the engine then supports.
    //
    // Must be sorted by ascending ShaderBinaryProfile and free of duplicates: that is the one
    // canonical payload encoding, so the content hash of a given binary set is stable across cooks.
    std::vector<AssetFormat::ShaderBinaryProfile> profiles{};
};

struct ShaderCompileProfileResult final {
    AssetFormat::ShaderBinaryProfile profile = AssetFormat::ShaderBinaryProfile::Invalid;
    Core::u32 byteCount = 0;
};

struct ShaderCompileResult final {
    std::vector<ShaderCompileProfileResult> profiles{};
    Core::u32 payloadByteCount = 0;
};

// Recipe-facing spelling accepted by --shader-kind: "Sprite2D", "Mesh3D".
[[nodiscard]] AssetFormat::ShaderKind parseShaderKindName(std::string_view name) noexcept;

// Whether this tool can drive shaderc for a profile at all. Dxbc50 needs the Windows SDK, so it is
// false off a Windows host; asking for it there fails the cook rather than silently dropping it.
[[nodiscard]] bool isShaderProfileSupportedOnHost(AssetFormat::ShaderBinaryProfile profile) noexcept;

// Compiles every requested profile, then writes one canonical payload file. A single failing profile
// fails the whole call.
[[nodiscard]] Core::Result<ShaderCompileResult> compileShaderPayload(const ShaderCompileRequest& request);

} // namespace Tina::AssetC
