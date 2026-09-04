#include "ShaderCompile.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/render/RenderDevice.hpp>

#include "core/io/PathUtil.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory_resource>
#include <new>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Tina::AssetC {
namespace {

using Core::u32;
using Core::usize;

// One shaderc invocation flavour. The suffix/platform/profile triples mirror
// tina_bgfx_shader_profiles() in cmake/TinaBgfxEmbeddedShaders.cmake, and must keep mirroring it:
// a custom shader that lacks a profile the engine's own shaders carry would fail to bind on
// exactly the renderer the engine still supports.
//
// `spv` uses platform=linux on purpose, matching what bgfx itself does for spirv.
struct ProfileSpec final {
    AssetFormat::ShaderBinaryProfile profile = AssetFormat::ShaderBinaryProfile::Invalid;
    std::string_view platform;
    std::string_view shadercProfile;
};

// Every profile this tool knows, ordered by ShaderBinaryProfile value because that is the order
// writeShaderPayloadBytes requires. Which entries a given cook actually runs is decided at runtime
// by ShaderCompileRequest::profiles, so a subset is always still ascending -- appending a spec
// whose enum value sorts before an earlier one would silently break that.
constexpr std::array<ProfileSpec, 5> kKnownProfiles{{
    {AssetFormat::ShaderBinaryProfile::Glsl120, "linux", "120"},
    {AssetFormat::ShaderBinaryProfile::SpirV, "linux", "spirv"},
    // DXBC needs shaderc's HLSL path, which needs the Windows SDK, so it is Windows-host only.
    {AssetFormat::ShaderBinaryProfile::Dxbc50, "windows", "s_5_0"},
    // OpenGL ES 3.0, matching the "essl|android|300_es" entry the engine's own shaders use when
    // TINA_RENDER_BGFX_MOBILE_SHADERS is on. bgfx reports Android GLES and iOS GLES as the same
    // RendererType, so one binary covers both.
    {AssetFormat::ShaderBinaryProfile::Essl300, "android", "300_es"},
    // Metal, the renderer bgfx selects on modern iOS now that Apple has deprecated OpenGL ES.
    // Cookable from any host: shaderc goes GLSL -> SPIR-V -> MSL through SPIRV-Cross and emits a
    // bgfx binary holding Metal *source*, so no Apple toolchain is involved. `metal` with no
    // version suffix is shaderc's own alias for 1210, matching bgfxToolUtils.cmake.
    {AssetFormat::ShaderBinaryProfile::Metal, "ios", "metal"},
}};

// The profiles a cook runs when the recipe names none: every profile this host can produce.
//
// Deliberately broader than tina_bgfx_shader_profiles(), which gates the mobile profiles behind
// TINA_RENDER_BGFX_MOBILE_SHADERS. That option is a property of the *build*, and it works for the
// engine's own shaders because they are embedded into the binary being built. A cooked payload is a
// shipped file: nothing about the cook says which renderer will load it, and the failure mode is
// asymmetric. A payload carrying a profile nobody selects costs a few hundred bytes; one missing the
// profile the device needs finds no matching blob and fails the upload outright, with the engine's
// own shaders working fine beside it -- which reads as "custom shaders are broken on this platform"
// rather than as a cook that was told the wrong thing.
//
// Only DXBC stays host-conditional, because shaderc's HLSL path genuinely needs the Windows SDK.
// ESSL and Metal cross-cook from any host: shaderc emits GLSL/MSL source, so neither needs the
// target's toolchain. A recipe that wants a narrower set still names profiles explicitly.
[[nodiscard]] std::array<AssetFormat::ShaderBinaryProfile, 5> defaultProfileStorage() noexcept
{
    // Ascending by enum value, which is the payload's own blob ordering requirement.
    return {
        AssetFormat::ShaderBinaryProfile::Glsl120,
        AssetFormat::ShaderBinaryProfile::SpirV,
#if defined(_WIN32)
        AssetFormat::ShaderBinaryProfile::Dxbc50,
#else
        AssetFormat::ShaderBinaryProfile::Invalid,
#endif
        AssetFormat::ShaderBinaryProfile::Essl300,
        AssetFormat::ShaderBinaryProfile::Metal,
    };
}

// Deep enough for any real contract chain (author header -> tina_*.sh -> bgfx_shader.sh is three)
// and shallow enough that a cyclic include fails with a message instead of exhausting the stack.
// The include-once set below already breaks simple cycles; this bounds the pathological rest.
constexpr Core::u32 MaxIncludeDepth = 16;

// Resolves one `#include` target the way shaderc does: the including file's own directory first,
// then each -i root in the order given. Empty when nothing matches.
[[nodiscard]] std::filesystem::path
resolveIncludePath(std::string_view target, const std::filesystem::path& includingFileDirectory,
                   const std::vector<std::string>& includeDirs)
{
    const std::filesystem::path relative{std::filesystem::path{std::string{target}}};
    std::error_code code{};
    const std::filesystem::path beside = includingFileDirectory / relative;
    if (std::filesystem::exists(beside, code) && !code)
    {
        return beside;
    }
    for (const std::string& root : includeDirs)
    {
        code.clear();
        const std::filesystem::path candidate = std::filesystem::path{root} / relative;
        if (std::filesystem::exists(candidate, code) && !code)
        {
            return candidate;
        }
    }
    return {};
}

// The include target between quotes or angle brackets on a directive line, or empty when the line is
// not an `#include`. `line` must already have its comments stripped.
[[nodiscard]] std::string_view includeTarget(std::string_view line) noexcept
{
    usize cursor = 0;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
    {
        ++cursor;
    }
    if (cursor >= line.size() || line[cursor] != '#')
    {
        return {};
    }
    ++cursor;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
    {
        ++cursor;
    }
    constexpr std::string_view keyword{"include"};
    if (line.substr(cursor, keyword.size()) != keyword)
    {
        return {};
    }
    cursor += keyword.size();
    // A bare `#includexyz` is a different directive, not this one.
    if (cursor < line.size() && (isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
    {
        return {};
    }
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
    {
        ++cursor;
    }
    if (cursor >= line.size())
    {
        return {};
    }
    const char opening = line[cursor];
    const char closing = opening == '<' ? '>' : (opening == '"' ? '"' : '\0');
    if (closing == '\0')
    {
        return {};
    }
    const usize begin = cursor + 1;
    const usize end = line.find(closing, begin);
    if (end == std::string_view::npos || end == begin)
    {
        return {};
    }
    return line.substr(begin, end - begin);
}

// Appends `path`'s text to `combined`, substituting each resolvable `#include` with the file it names.
//
// Why the cook expands includes at all: the sampler-register rule is enforced by scanning source
// text, and a scan of the top-level file alone cannot see a sampler the author declared in a header
// of their own. That shape cooks clean today and samples the wrong stage on D3D11, Vulkan and Metal
// -- the exact defect the check exists to stop, reintroduced by moving one line into a header.
//
// Engine headers are expanded too rather than skipped. Their declarations are already legal by
// construction (s_tex at 0, s_normalTex at 1), so including them costs nothing, and hard-coding a
// list of "engine" filenames here would be a second copy of the engine sampler set that could drift
// from GpuShaderTextureStages.
//
// A missing include is *not* an error here: shaderc is the authority on that, and it reports the file
// and line. Failing first would replace its diagnostic with a worse one.
Core::Status gatherShaderSourceWithIncludes(const std::filesystem::path& path,
                                            const std::vector<std::string>& includeDirs,
                                            Core::u32 depth,
                                            std::vector<std::filesystem::path>& visited,
                                            std::string& combined)
{
    if (depth > MaxIncludeDepth)
    {
        return Core::failure(Asset::AssetErrorCode::ShaderCompileFailed,
                             "shader include nesting is deeper than the cooker will follow, which "
                             "usually means two headers include each other");
    }

    std::error_code code{};
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, code);
    if (code)
    {
        canonical = path;
    }
    // Include-once, matching the header guards these files carry anyway. Without it a diamond would
    // duplicate every declaration and trip the duplicate-name check on a shader that compiles.
    if (std::find(visited.begin(), visited.end(), canonical) != visited.end())
    {
        return Core::success();
    }
    visited.push_back(canonical);

    auto contents = Core::readFile(
        Core::Detail::pathToUtf8(path),
        Core::ReadFileConfig{.maxBytes = AssetFormat::ShaderWire::MaxBlobBytes,
                             .memoryResource = std::pmr::new_delete_resource()});
    if (!contents)
    {
        return Core::failure(std::move(contents.error()));
    }
    const std::string_view text{reinterpret_cast<const char*>(contents->data()), contents->size()};

    // Substituted in place, at the line the directive sits on, rather than appended after this file.
    // Position is load-bearing twice over: the scan marks a sampler as referenced only by a mention
    // that comes *after* its declaration, so a header declaring what this file samples would look
    // never-sampled if it landed at the end; and the author sampler sequence is positional, so the
    // order the scan walks declarations in has to be the order the preprocessor produces.
    const std::filesystem::path directory = path.parent_path();
    usize lineBegin = 0;
    while (lineBegin <= text.size())
    {
        const usize lineEnd = text.find('\n', lineBegin);
        const bool lastLine = lineEnd == std::string_view::npos;
        const std::string_view line =
            text.substr(lineBegin, lastLine ? std::string_view::npos : lineEnd - lineBegin);

        const std::string_view target = includeTarget(line);
        std::filesystem::path resolved{};
        if (!target.empty())
        {
            resolved = resolveIncludePath(target, directory, includeDirs);
        }

        if (resolved.empty())
        {
            // Kept verbatim, including an include that did not resolve: shaderc is the authority on a
            // missing header and reports the file and line, so dropping it here would replace its
            // diagnostic with silence.
            combined.append(line);
            combined.push_back('\n');
        }
        else
        {
            if (auto status =
                    gatherShaderSourceWithIncludes(resolved, includeDirs, depth + 1, visited, combined);
                !status)
            {
                return status;
            }
        }

        if (lastLine)
        {
            break;
        }
        lineBegin = lineEnd + 1;
    }
    return Core::success();
}

[[nodiscard]] const ProfileSpec* findProfileSpec(AssetFormat::ShaderBinaryProfile profile) noexcept
{
    for (const ProfileSpec& spec : kKnownProfiles)
    {
        if (spec.profile == profile)
        {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] std::filesystem::path temporaryOutputPath(const std::filesystem::path& directory,
                                                        AssetFormat::ShaderBinaryProfile profile)
{
    std::string name{"tina_shaderc_"};
    name.append(AssetFormat::shaderBinaryProfileName(profile));
    name.append(".bin");
    return directory / std::filesystem::path{name};
}

// Result of running a child to completion. `output` holds stdout and stderr interleaved, because
// shaderc writes its diagnostics to both and an author needs them in the order produced.
struct ProcessResult final {
    int exitCode = 0;
    std::string output;
};

#if defined(_WIN32)

// Quotes one argv element per the CommandLineToArgvW rules that CreateProcessW's callee will use to
// split it again. Backslashes are literal except when they run into the closing quote, where each
// must be doubled -- otherwise a path ending in a separator escapes that quote and the child sees
// the following argument glued onto this one.
void appendQuotedArgument(std::string& commandLine, std::string_view argument)
{
    const bool needsQuotes =
        argument.empty() || argument.find_first_of(" \t\n\v\"") != std::string_view::npos;
    if (!needsQuotes)
    {
        commandLine.append(argument);
        return;
    }
    commandLine.push_back('"');
    for (usize index = 0; index < argument.size(); ++index)
    {
        usize backslashes = 0;
        while (index < argument.size() && argument[index] == '\\')
        {
            ++index;
            ++backslashes;
        }
        if (index == argument.size())
        {
            commandLine.append(backslashes * 2U, '\\');
            break;
        }
        if (argument[index] == '"')
        {
            commandLine.append(backslashes * 2U + 1U, '\\');
        }
        else
        {
            commandLine.append(backslashes, '\\');
        }
        commandLine.push_back(argument[index]);
    }
    commandLine.push_back('"');
}

[[nodiscard]] Core::Result<ProcessResult> runProcess(std::span<const std::string> arguments)
{
    std::string commandLine;
    for (usize index = 0; index < arguments.size(); ++index)
    {
        if (index != 0)
        {
            commandLine.push_back(' ');
        }
        if (index == 0)
        {
            // CreateProcessW parses the module name out of the command line itself, and it does not
            // accept forward slashes there -- a path like out/build/bin/shaderc.exe fails to launch
            // with ERROR_FILE_NOT_FOUND even though every other Win32 file API opens it. Callers get
            // forward slashes from CMake and from any POSIX-style shell, so normalize rather than
            // making it their problem. The remaining arguments are shaderc's own and are left alone.
            appendQuotedArgument(
                commandLine,
                Core::Detail::pathToUtf8(
                    Core::Detail::pathFromUtf8Bytes(arguments[index]).make_preferred()));
            continue;
        }
        appendQuotedArgument(commandLine, arguments[index]);
    }

    SECURITY_ATTRIBUTES pipeAttributes{};
    pipeAttributes.nLength = sizeof(pipeAttributes);
    pipeAttributes.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (::CreatePipe(&readEnd, &writeEnd, &pipeAttributes, 0) == 0)
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to create a shaderc output pipe"}
                                 .setNativeCode(static_cast<Core::i64>(::GetLastError())));
    }
    // The child must not inherit the read end, or the pipe never reports EOF: our own copy of the
    // write handle is closed below, but a copy living in the child would keep it open forever.
    if (::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0) == 0)
    {
        const auto lastError = ::GetLastError();
        ::CloseHandle(readEnd);
        ::CloseHandle(writeEnd);
        return Core::failure(
            Core::Error{Core::CoreErrorCode::Io, "failed to configure the shaderc output pipe"}
                .setNativeCode(static_cast<Core::i64>(lastError)));
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writeEnd;
    startupInfo.hStdError = writeEnd;
    startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    // Widened explicitly rather than through std::filesystem::path: the command line is not a path,
    // and CreateProcessW needs a mutable buffer it may write into.
    std::wstring wideCommandLine;
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, commandLine.data(),
                                                 static_cast<int>(commandLine.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        ::CloseHandle(readEnd);
        ::CloseHandle(writeEnd);
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "shaderc command line is not valid UTF-8");
    }
    try
    {
        wideCommandLine.resize(static_cast<usize>(wideLength) + 1U, L'\0');
    }
    catch (const std::bad_alloc&)
    {
        ::CloseHandle(readEnd);
        ::CloseHandle(writeEnd);
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc command line allocation failed");
    }
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, commandLine.data(),
                          static_cast<int>(commandLine.size()), wideCommandLine.data(), wideLength);

    PROCESS_INFORMATION processInfo{};
    const BOOL created = ::CreateProcessW(nullptr, wideCommandLine.data(), nullptr, nullptr, TRUE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo,
                                          &processInfo);
    // Closed in the parent either way: on success the child owns its copy, and while it stays open
    // here a read on the pipe would block past the child's exit instead of seeing EOF.
    ::CloseHandle(writeEnd);
    if (created == 0)
    {
        const auto lastError = ::GetLastError();
        ::CloseHandle(readEnd);
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to launch shaderc"}
                                 .setNativeCode(static_cast<Core::i64>(lastError)));
    }

    ProcessResult result{};
    try
    {
        std::array<char, 4096> buffer{};
        for (;;)
        {
            DWORD readBytes = 0;
            if (::ReadFile(readEnd, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes,
                           nullptr) == 0)
            {
                // ERROR_BROKEN_PIPE is the normal end: the child closed its last write handle.
                break;
            }
            if (readBytes == 0)
            {
                break;
            }
            result.output.append(buffer.data(), readBytes);
        }
    }
    catch (const std::bad_alloc&)
    {
        ::CloseHandle(readEnd);
        ::CloseHandle(processInfo.hThread);
        ::CloseHandle(processInfo.hProcess);
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc output allocation failed");
    }
    ::CloseHandle(readEnd);

    ::WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL queried = ::GetExitCodeProcess(processInfo.hProcess, &exitCode);
    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
    if (queried == 0)
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to read the shaderc exit code"}
                                 .setNativeCode(static_cast<Core::i64>(::GetLastError())));
    }
    result.exitCode = static_cast<int>(exitCode);
    return result;
}

#else

[[nodiscard]] Core::Result<ProcessResult> runProcess(std::span<const std::string> arguments)
{
    std::array<int, 2> pipeFds{-1, -1};
    if (::pipe(pipeFds.data()) != 0)
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to create a shaderc output pipe"}
                                 .setNativeCode(static_cast<Core::i64>(errno)));
    }

    posix_spawn_file_actions_t fileActions{};
    if (::posix_spawn_file_actions_init(&fileActions) != 0)
    {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return Core::failure(Core::CoreErrorCode::Io, "failed to init shaderc spawn file actions");
    }
    // The read end must not survive into the child, or the pipe never reaches EOF.
    ::posix_spawn_file_actions_addclose(&fileActions, pipeFds[0]);
    ::posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&fileActions, pipeFds[1]);

    std::vector<char*> argv;
    ProcessResult result{};
    pid_t childPid = -1;
    int spawnError = 0;
    try
    {
        argv.reserve(arguments.size() + 1U);
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        spawnError = ::posix_spawn(&childPid, argv[0], &fileActions, nullptr, argv.data(), environ);
    }
    catch (const std::bad_alloc&)
    {
        ::posix_spawn_file_actions_destroy(&fileActions);
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc argv allocation failed");
    }
    ::posix_spawn_file_actions_destroy(&fileActions);
    ::close(pipeFds[1]);
    if (spawnError != 0)
    {
        ::close(pipeFds[0]);
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to launch shaderc"}
                                 .setNativeCode(static_cast<Core::i64>(spawnError)));
    }

    try
    {
        std::array<char, 4096> buffer{};
        for (;;)
        {
            const auto readBytes = ::read(pipeFds[0], buffer.data(), buffer.size());
            if (readBytes < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            if (readBytes == 0)
            {
                break;
            }
            result.output.append(buffer.data(), static_cast<usize>(readBytes));
        }
    }
    catch (const std::bad_alloc&)
    {
        ::close(pipeFds[0]);
        int discarded = 0;
        ::waitpid(childPid, &discarded, 0);
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc output allocation failed");
    }
    ::close(pipeFds[0]);

    int waitStatus = 0;
    while (::waitpid(childPid, &waitStatus, 0) < 0)
    {
        if (errno != EINTR)
        {
            return Core::failure(Core::Error{Core::CoreErrorCode::Io, "failed to reap shaderc"}
                                     .setNativeCode(static_cast<Core::i64>(errno)));
        }
    }
    // A signalled child is not a compile error: report it as one so the message does not read as
    // "shaderc rejected your shader" when the process was killed.
    if (WIFSIGNALED(waitStatus))
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "shaderc terminated by a signal"}
                                 .setNativeCode(static_cast<Core::i64>(WTERMSIG(waitStatus))));
    }
    result.exitCode = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : 1;
    return result;
}

#endif

} // namespace

AssetFormat::ShaderKind parseShaderKindName(std::string_view name) noexcept
{
    if (name == "Sprite2D")
    {
        return AssetFormat::ShaderKind::Sprite2D;
    }
    if (name == "Mesh3D")
    {
        return AssetFormat::ShaderKind::Mesh3D;
    }
    return AssetFormat::ShaderKind::Invalid;
}

bool isShaderProfileSupportedOnHost(AssetFormat::ShaderBinaryProfile profile) noexcept
{
    if (findProfileSpec(profile) == nullptr)
    {
        return false;
    }
#if defined(_WIN32)
    return true;
#else
    return profile != AssetFormat::ShaderBinaryProfile::Dxbc50;
#endif
}

Core::Result<ShaderCompileResult> compileShaderPayload(const ShaderCompileRequest& request)
{
    if (request.shadercPath.empty() || request.sourcePath.empty() ||
        request.varyingDefPath.empty() || request.outputPath.empty())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "shader compile requires shaderc, source, varying def and output paths");
    }
    if (request.includeDirs.empty())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "shader compile requires at least one include directory");
    }
    if (request.shaderKind != AssetFormat::ShaderKind::Sprite2D &&
        request.shaderKind != AssetFormat::ShaderKind::Mesh3D)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "shader compile requires a supported shader kind");
    }

    // Before shaderc, not after: a sampler whose declared register disagrees with the stage the
    // engine binds compiles cleanly on every profile and is only wrong on the GPU. Cooking it and
    // reporting nothing would ship a shader that samples an engine texture on D3D11 and Vulkan while
    // looking correct on GL, which is the one failure no test on this host can see.
    {
        // Includes expanded, because a sampler declared in a header of the author's own is invisible
        // to a scan of the top-level file and would ship the very stage mismatch this check exists to
        // catch. Order matters and is preserved: a file's own text comes before what it includes, so
        // the declaration sequence the scan sees matches the order shaderc's preprocessor produces --
        // which is the order the backend numbers stages in.
        std::string sourceText;
        std::vector<std::filesystem::path> visited;
        if (auto gathered = gatherShaderSourceWithIncludes(
                std::filesystem::path{request.sourcePath}, request.includeDirs, 0, visited, sourceText);
            !gathered)
        {
            return Core::failure(std::move(gathered.error()));
        }
        auto declarations = Render::parseShaderSamplerDeclarations(sourceText);
        if (!declarations)
        {
            return Core::failure(
                Core::Error{Asset::AssetErrorCode::ShaderCompileFailed, declarations.error().message});
        }
        const Render::GpuShaderKind renderKind =
            request.shaderKind == AssetFormat::ShaderKind::Mesh3D ? Render::GpuShaderKind::Mesh3D
                                                                  : Render::GpuShaderKind::Sprite2D;
        if (auto checked = Render::validateAuthorSamplerRegisters(renderKind, *declarations); !checked)
        {
            return Core::failure(
                Core::Error{Asset::AssetErrorCode::ShaderCompileFailed, checked.error().message});
        }
    }

    std::vector<ProfileSpec> specs;
    std::vector<std::vector<std::byte>> blobBytes;
    std::vector<AssetFormat::ShaderBlobDesc> blobDescs;
    ShaderCompileResult result{};
    std::filesystem::path scratchDirectory;
    try
    {
        const auto defaults = defaultProfileStorage();
        std::vector<AssetFormat::ShaderBinaryProfile> requested;
        if (request.profiles.empty())
        {
            for (const AssetFormat::ShaderBinaryProfile profile : defaults)
            {
                if (profile != AssetFormat::ShaderBinaryProfile::Invalid)
                {
                    requested.push_back(profile);
                }
            }
        }
        else
        {
            requested.assign(request.profiles.begin(), request.profiles.end());
        }
        if (requested.empty())
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "shader compile requires at least one renderer profile");
        }

        specs.reserve(requested.size());
        auto previousProfile = AssetFormat::ShaderBinaryProfile::Invalid;
        for (const AssetFormat::ShaderBinaryProfile profile : requested)
        {
            // Ascending and duplicate-free is the payload's own requirement. Checking it here names
            // the offending profile, instead of letting writeShaderPayloadBytes reject the finished
            // table after every shaderc invocation has already run.
            if (static_cast<Core::u16>(profile) <= static_cast<Core::u16>(previousProfile))
            {
                std::string message{"shader profiles must be sorted ascending without duplicates, got "};
                message.append(AssetFormat::shaderBinaryProfileName(profile));
                return Core::failure(Core::CoreErrorCode::InvalidArgument, message);
            }
            previousProfile = profile;

            const ProfileSpec* spec = findProfileSpec(profile);
            if (spec == nullptr || !isShaderProfileSupportedOnHost(profile))
            {
                std::string message{"shader profile is not supported on this host: "};
                message.append(AssetFormat::shaderBinaryProfileName(profile));
                return Core::failure(Core::CoreErrorCode::InvalidArgument, message);
            }
            specs.push_back(*spec);
        }
        blobBytes.reserve(specs.size());
        blobDescs.reserve(specs.size());
        result.profiles.reserve(specs.size());
        // Intermediates live beside the payload, not in the system temp dir: the payload's directory
        // is already known writable, and a scratch file there is visible to whoever debugs the cook.
        scratchDirectory = Core::Detail::pathFromUtf8Bytes(request.outputPath).parent_path();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shader compile allocation failed");
    }

    if (const auto status = Core::createParentDirectories(request.outputPath); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    for (const ProfileSpec& spec : specs)
    {
        std::filesystem::path profileOutput;
        std::vector<std::string> arguments;
        try
        {
            profileOutput = temporaryOutputPath(scratchDirectory, spec.profile);
            arguments.reserve(18U + request.includeDirs.size() * 2U);
            arguments.push_back(request.shadercPath);
            arguments.emplace_back("--type");
            arguments.emplace_back("fragment");
            arguments.emplace_back("--platform");
            arguments.emplace_back(spec.platform);
            arguments.emplace_back("--profile");
            arguments.emplace_back(spec.shadercProfile);
            arguments.emplace_back("--varyingdef");
            arguments.push_back(request.varyingDefPath);
            for (const std::string& includeDir : request.includeDirs)
            {
                arguments.emplace_back("-i");
                arguments.push_back(includeDir);
            }
            arguments.emplace_back("-f");
            arguments.push_back(request.sourcePath);
            arguments.emplace_back("-o");
            arguments.push_back(Core::Detail::pathToUtf8(profileOutput));
            // --Werror matches how the engine's own shaders are cooked, so a warning cannot reach a
            // shipped binary. -O 3 is fixed rather than tied to the build config, because varying
            // the optimisation level would change binaries and move pixel fingerprints.
            arguments.emplace_back("--Werror");
            arguments.emplace_back("-O");
            arguments.emplace_back("3");
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc argument allocation failed");
        }

        auto process = runProcess(arguments);
        if (!process)
        {
            return Core::failure(std::move(process.error()));
        }
        if (process->exitCode != 0)
        {
            std::error_code removeError;
            std::filesystem::remove(profileOutput, removeError);
            std::string message{"shaderc failed for profile "};
            try
            {
                message.append(AssetFormat::shaderBinaryProfileName(spec.profile));
                message.append(": ");
                message.append(process->output);
            }
            catch (const std::bad_alloc&)
            {
                return Core::failure(Core::CoreErrorCode::OutOfMemory, "shaderc message allocation failed");
            }
            return Core::failure(Core::Error{Asset::AssetErrorCode::ShaderCompileFailed, message}
                                     .setNativeCode(process->exitCode));
        }

        // readFile rejects a null memory resource, so one must be named even for a short read.
        auto bytes = Core::readFile(
            Core::Detail::pathToUtf8(profileOutput),
            Core::ReadFileConfig{.maxBytes = AssetFormat::ShaderWire::MaxBlobBytes,
                                 .memoryResource = std::pmr::new_delete_resource()});
        std::error_code removeError;
        std::filesystem::remove(profileOutput, removeError);
        if (!bytes)
        {
            return Core::failure(std::move(bytes.error()));
        }
        try
        {
            blobBytes.emplace_back(bytes->begin(), bytes->end());
            result.profiles.push_back(ShaderCompileProfileResult{
                .profile = spec.profile,
                .byteCount = static_cast<u32>(blobBytes.back().size()),
            });
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "shader binary allocation failed");
        }
    }

    // Built after every read, because blobBytes may reallocate while profiles are still compiling
    // and a span taken earlier would dangle.
    try
    {
        for (usize index = 0; index < specs.size(); ++index)
        {
            blobDescs.push_back(AssetFormat::ShaderBlobDesc{
                .profile = specs[index].profile,
                .bytes = blobBytes[index],
            });
        }
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shader blob table allocation failed");
    }

    auto payload = AssetFormat::writeShaderPayloadBytes(AssetFormat::ShaderPayloadDesc{
        .shaderKind = request.shaderKind,
        .stage = AssetFormat::ShaderStage::Fragment,
        .blobs = blobDescs,
    });
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    if (const auto status = Core::writeFile(request.outputPath, *payload); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    result.payloadByteCount = static_cast<u32>(payload->size());
    return result;
}

} // namespace Tina::AssetC
