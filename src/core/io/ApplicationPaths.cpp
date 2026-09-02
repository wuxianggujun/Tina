#include <tina/core/io/ApplicationPaths.hpp>

#include "PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <string>
#include <string_view>
#include <vector>

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
#include <unistd.h>
#endif

namespace Tina::Core {
namespace {

[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string_view message)
{
    return Core::failure(code, message);
}

[[nodiscard]] std::unexpected<Error> failNative(ErrorCode code, std::string_view message, i64 nativeCode)
{
    Error error{code, std::string{message}};
    error.setNativeCode(nativeCode);
    return std::unexpected<Error>{std::move(error)};
}

using Detail::isSafeRelativeContentPath;

// Keeps the parent of the OS-reported executable path. Separators are left as the
// OS spelled them, matching userApplicationDirectory, so both '/' and '\' count
// as one here.
//
// A root directory keeps its separator, because dropping it changes what the path
// means rather than just how it looks: "" is relative, and "C:" names the working
// directory of drive C rather than its root.
[[nodiscard]] bool stripFileName(std::string& path) noexcept
{
    const usize separator = path.find_last_of("/\\");
    if (separator == std::string::npos)
    {
        return false;
    }
    if (separator == 0)
    {
        path.resize(1);
        return true;
    }
    if (path[separator - 1U] == ':')
    {
        path.resize(separator + 1U);
        return true;
    }
    path.resize(separator);
    return true;
}

#if defined(_WIN32)

// GetModuleFileNameW truncates instead of reporting the length it needed, and
// signals that by filling the buffer exactly. Grow until it fits, bounded by the
// longest path Windows can express.
constexpr DWORD MaximumModulePathLength = 32768;

[[nodiscard]] Result<std::string> executablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            return failNative(CoreErrorCode::NotFound, "the running module path is unavailable",
                              static_cast<i64>(::GetLastError()));
        }
        if (written < buffer.size())
        {
            const std::wstring_view native{buffer.data(), written};
            std::u16string utf16;
            utf16.reserve(native.size());
            for (const wchar_t unit : native)
            {
                utf16.push_back(static_cast<char16_t>(unit));
            }

            // Worst case is three UTF-8 bytes per UTF-16 unit: a surrogate pair
            // spends two units on four bytes, which is less per unit.
            std::string utf8(utf16.size() * 3U, '\0');
            const auto length = convertUtf16ToStrictUtf8(utf16, utf8);
            if (!length)
            {
                return fail(CoreErrorCode::Internal,
                            "the running module path is not representable as UTF-8");
            }
            utf8.resize(*length);
            return utf8;
        }
        if (buffer.size() >= MaximumModulePathLength)
        {
            return fail(CoreErrorCode::Internal, "the running module path exceeds the Windows path limit");
        }
        buffer.resize(buffer.size() * 2U > MaximumModulePathLength ? MaximumModulePathLength
                                                                  : buffer.size() * 2U);
    }
}

#else

// readlink neither NUL-terminates nor reports the length it needed, and signals
// truncation by filling the buffer exactly, so this grows the same way the
// Windows path does.
constexpr usize MaximumExecutablePathLength = 65536;

[[nodiscard]] Result<std::string> executablePath()
{
    std::string buffer(1024, '\0');
    for (;;)
    {
        const ssize_t written = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (written < 0)
        {
            return failNative(CoreErrorCode::NotFound, "/proc/self/exe does not name the running executable",
                              static_cast<i64>(errno));
        }
        const auto length = static_cast<usize>(written);
        if (length < buffer.size())
        {
            buffer.resize(length);
            if (!isStrictUtf8WithoutNul(buffer))
            {
                return fail(CoreErrorCode::Internal,
                            "the running executable path is not valid UTF-8");
            }
            return buffer;
        }
        if (buffer.size() >= MaximumExecutablePathLength)
        {
            return fail(CoreErrorCode::Internal, "the running executable path is implausibly long");
        }
        buffer.resize(buffer.size() * 2U > MaximumExecutablePathLength ? MaximumExecutablePathLength
                                                                      : buffer.size() * 2U);
    }
}

#endif

} // namespace

Result<std::string> applicationDirectory()
try
{
    auto path = executablePath();
    if (!path)
    {
        return Core::failure(std::move(path.error()));
    }
    if (!stripFileName(*path))
    {
        return fail(CoreErrorCode::Internal, "the running executable path has no parent directory");
    }
    return std::move(*path);
}
catch (const std::bad_alloc&)
{
    return fail(CoreErrorCode::OutOfMemory, "application directory allocation failed");
}

Result<std::string> applicationFilePath(std::string_view relativePath)
try
{
    if (!isSafeRelativeContentPath(relativePath))
    {
        return fail(CoreErrorCode::InvalidArgument,
                    "application relative path must be UTF-8, '/'-separated, and below the executable "
                    "directory");
    }
    auto directory = applicationDirectory();
    if (!directory)
    {
        return Core::failure(std::move(directory.error()));
    }
    return Detail::joinContentPath(std::move(*directory), relativePath);
}
catch (const std::bad_alloc&)
{
    return fail(CoreErrorCode::OutOfMemory, "application file path allocation failed");
}

} // namespace Tina::Core
