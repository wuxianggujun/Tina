#include "WindowsShellReveal.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/platform/PlatformErrors.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT
#include <windows.h>
#include <shlobj.h>

#include <limits>
#include <new>
#include <string>
#include <thread>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] Core::Error hresultError(std::string_view message, HRESULT hr) noexcept
{
    Core::Error error{PlatformErrorCode::ShellRevealFailed, message};
    error.setNativeCode(static_cast<i64>(hr));
    return error;
}

[[nodiscard]] Core::Result<std::wstring> wideFromUtf8(std::string_view utf8)
{
    if (!Core::isStrictUtf8WithoutNul(utf8) || utf8.empty() ||
        utf8.size() > static_cast<usize>((std::numeric_limits<int>::max)()))
    {
        return Core::failure(PlatformErrorCode::ShellRevealPathInvalid,
                             "Shell reveal path must be non-empty strict UTF-8 without NUL");
    }
    const int sourceLength = static_cast<int>(utf8.size());
    const int wideLength = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), sourceLength, nullptr, 0);
    if (wideLength <= 0)
    {
        return Core::failure(hresultError("Shell reveal UTF-8 conversion failed",
                                          HRESULT_FROM_WIN32(::GetLastError())));
    }
    try
    {
        std::wstring wide(static_cast<usize>(wideLength), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), sourceLength,
                                  wide.data(), wideLength) != wideLength)
        {
            return Core::failure(hresultError("Shell reveal UTF-8 conversion failed",
                                              HRESULT_FROM_WIN32(::GetLastError())));
        }
        return wide;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Shell reveal UTF-16 allocation failed");
    }
}

[[nodiscard]] bool isWindowsAbsolutePath(std::wstring_view path) noexcept
{
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        return true;
    }
    return path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
                                (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

} // namespace

Core::Status WindowsShellReveal::revealPath(std::string_view utf8AbsolutePath)
{
    if (std::this_thread::get_id() != ownerThread_)
    {
        return Core::failure(PlatformErrorCode::WrongOwnerThread,
                             "Shell reveal must run on the creating thread");
    }
    auto wide = wideFromUtf8(utf8AbsolutePath);
    if (!wide)
    {
        return Core::failure(std::move(wide.error()));
    }
    if (!isWindowsAbsolutePath(*wide))
    {
        return Core::failure(PlatformErrorCode::ShellRevealPathInvalid,
                             "Shell reveal path must be an absolute Windows path");
    }
    if (::GetFileAttributesW(wide->c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        Core::Error error{PlatformErrorCode::ShellRevealFailed,
                          "Shell reveal path does not exist"};
        error.setNativeCode(static_cast<i64>(HRESULT_FROM_WIN32(::GetLastError())));
        return Core::failure(std::move(error));
    }

    const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = init == S_OK;
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
    {
        return Core::failure(hresultError("Shell reveal could not initialize COM", init));
    }
    auto uninitGuard = Core::makeScopeExit([uninitialize]() noexcept {
        if (uninitialize)
        {
            ::CoUninitialize();
        }
    });

    PIDLIST_ABSOLUTE item = ::ILCreateFromPathW(wide->c_str());
    if (item == nullptr)
    {
        return Core::failure(PlatformErrorCode::ShellRevealFailed,
                             "Shell reveal could not create an item identifier for the path");
    }
    auto freeItem = Core::makeScopeExit([item]() noexcept { ::ILFree(item); });
    const HRESULT opened = ::SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    if (FAILED(opened))
    {
        return Core::failure(hresultError("Shell reveal could not open the file manager", opened));
    }
    return Core::success();
}

} // namespace Tina::Platform::Detail
