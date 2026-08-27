#include <tina/core/io/UserPaths.hpp>

#include <tina/core/text/Utf8.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Core {
namespace {

[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string_view message)
{
    return Core::failure(code, message);
}

// A segment is joined verbatim, so anything that could redirect the result out of
// the resolved base directory is rejected instead of sanitized.
[[nodiscard]] bool isSafePathSegment(std::string_view segment) noexcept
{
    if (segment.empty() || segment == "." || segment == "..")
    {
        return false;
    }
    if (!isStrictUtf8WithoutNul(segment))
    {
        return false;
    }
    return segment.find('/') == std::string_view::npos &&
           segment.find('\\') == std::string_view::npos &&
           segment.find(':') == std::string_view::npos;
}

[[nodiscard]] std::string_view environmentValue(const char* name) noexcept
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return {};
    }
    return std::string_view{value};
}

// An environment base must be absolute. A relative value would resolve against
// whatever the process working directory happens to be, which is not a per-user
// location and would silently scatter settings.
[[nodiscard]] bool isUsableBase(std::string_view base) noexcept
{
    if (base.empty() || !isStrictUtf8WithoutNul(base))
    {
        return false;
    }
    std::u8string bytes;
    bytes.reserve(base.size());
    for (const char byte : base)
    {
        bytes.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    const std::filesystem::path candidate{std::move(bytes)};
    return candidate.is_absolute() && !candidate.empty();
}

[[nodiscard]] std::string joinWithForwardSlash(std::string_view base, std::string_view segment)
{
    std::string joined{base};
    while (!joined.empty() && (joined.back() == '/' || joined.back() == '\\'))
    {
        joined.pop_back();
    }
    joined.push_back('/');
    joined.append(segment);
    return joined;
}

} // namespace

Result<std::string> userApplicationDirectory(std::string_view applicationName, UserDirectoryKind kind)
try
{
    if (!isSafePathSegment(applicationName))
    {
        return fail(CoreErrorCode::InvalidArgument,
                    "user application name must be one UTF-8 path segment without separators");
    }
    if (kind != UserDirectoryKind::Config && kind != UserDirectoryKind::State)
    {
        return fail(CoreErrorCode::InvalidArgument, "unknown user directory kind");
    }

#if defined(_WIN32)
    // LOCALAPPDATA is the per-machine, non-roaming location and is the better
    // default for both kinds; APPDATA is the roaming fallback.
    const std::array<const char*, 2> candidates{"LOCALAPPDATA", "APPDATA"};
    for (const char* name : candidates)
    {
        const std::string_view base = environmentValue(name);
        if (isUsableBase(base))
        {
            return joinWithForwardSlash(base, applicationName);
        }
    }
    return fail(CoreErrorCode::NotFound,
                "neither LOCALAPPDATA nor APPDATA provides an absolute per-user directory");
#else
    const char* explicitName =
        kind == UserDirectoryKind::Config ? "XDG_CONFIG_HOME" : "XDG_STATE_HOME";
    const std::string_view explicitBase = environmentValue(explicitName);
    if (isUsableBase(explicitBase))
    {
        return joinWithForwardSlash(explicitBase, applicationName);
    }
    const std::string_view home = environmentValue("HOME");
    if (!isUsableBase(home))
    {
        return fail(CoreErrorCode::NotFound,
                    "neither the XDG base directory nor HOME provides an absolute per-user directory");
    }
    const std::string_view relative =
        kind == UserDirectoryKind::Config ? ".config" : ".local/state";
    return joinWithForwardSlash(joinWithForwardSlash(home, relative), applicationName);
#endif
}
catch (const std::bad_alloc&)
{
    return fail(CoreErrorCode::OutOfMemory, "user application directory allocation failed");
}

Result<std::string> userApplicationFilePath(std::string_view applicationName, std::string_view fileName,
                                            UserDirectoryKind kind)
try
{
    if (!isSafePathSegment(fileName))
    {
        return fail(CoreErrorCode::InvalidArgument,
                    "user application file name must be one UTF-8 path segment without separators");
    }
    auto directory = userApplicationDirectory(applicationName, kind);
    if (!directory)
    {
        return Core::failure(std::move(directory.error()));
    }
    return joinWithForwardSlash(*directory, fileName);
}
catch (const std::bad_alloc&)
{
    return fail(CoreErrorCode::OutOfMemory, "user application file path allocation failed");
}

} // namespace Tina::Core
