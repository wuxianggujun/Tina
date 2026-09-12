#include <tina/core/io/WriteFile.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/base/ScopeExit.hpp>

#include "PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Tina::Core {
namespace {

[[nodiscard]] bool containsEmbeddedNul(std::string_view text) noexcept
{
    return text.find('\0') != std::string_view::npos;
}

[[nodiscard]] Error makeIoError(std::string_view message, std::error_code errorCode)
{
    Error error{CoreErrorCode::Io, message};
    if (errorCode)
    {
        error.setNativeCode(static_cast<i64>(errorCode.value()));
        error.addContext("native", errorCode.message());
    }
    return error;
}

[[nodiscard]] Status validateUtf8Path(std::string_view utf8Path)
{
    if (utf8Path.empty() || containsEmbeddedNul(utf8Path))
    {
        return failure(CoreErrorCode::InvalidArgument, "file path must be non-empty UTF-8 without embedded NUL");
    }
    if (!countStrictUtf8CodepointsWithoutNul(utf8Path))
    {
        return failure(CoreErrorCode::InvalidArgument, "file path is not strict UTF-8");
    }
    return success();
}

[[nodiscard]] std::string makeTempFileName()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    static std::atomic<u64> sequence{0};
    std::mt19937_64 rng{static_cast<Tina::Core::u64>(now) ^ 0x9E3779B97F4A7C15ULL};
    const auto token = rng();
    return ".tina_write_" + std::to_string(token) + "_" +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
}

[[nodiscard]] Status replaceFileAtomically(const std::filesystem::path& tempPath,
                                           const std::filesystem::path& finalPath)
{
#if defined(_WIN32)
    // MoveFileEx(REPLACE_EXISTING) fails with ERROR_ACCESS_DENIED when the destination
    // has a live mapped data section, even when its handle shares DELETE. Windows 10+
    // POSIX rename semantics atomically replaces the directory entry while old mappings
    // retain the previous file. Never unmap readers or delete the live path first.
    const auto target = std::filesystem::absolute(finalPath).native();
    constexpr usize PrefixBytes = offsetof(FILE_RENAME_INFO, FileName);
    if (target.size() > ((std::numeric_limits<DWORD>::max)() - PrefixBytes - sizeof(wchar_t)) / sizeof(wchar_t))
        return failure(CoreErrorCode::CapacityExceeded, "atomic replacement path is too long");
    const auto filenameBytes = target.size() * sizeof(wchar_t);
    // Win32 consumes a NUL-terminated name even though FileNameLength excludes the
    // terminator. A counted-only buffer can rename to an unintended heap-tail suffix.
    std::vector<std::byte> buffer(PrefixBytes + filenameBytes + sizeof(wchar_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(filenameBytes);
    std::memcpy(rename->FileName, target.data(), filenameBytes);
    const HANDLE source = ::CreateFileW(tempPath.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (source == INVALID_HANDLE_VALUE)
        return failure(makeIoError("failed to open atomic replacement source",
                                    {static_cast<int>(::GetLastError()), std::system_category()}));
    const auto closeSource = makeScopeExit([source]() noexcept { ::CloseHandle(source); });
    if (!::SetFileInformationByHandle(source, FileRenameInfoEx, rename, static_cast<DWORD>(buffer.size())))
    {
        const std::error_code errorCode{static_cast<int>(::GetLastError()), std::system_category()};
        return failure(makeIoError("failed to atomically rename replacement file", errorCode));
    }
#else
    std::error_code errorCode;
    std::filesystem::rename(tempPath, finalPath, errorCode);
    if (errorCode)
    {
        return failure(makeIoError("failed to atomically replace target file", errorCode));
    }
#endif
    return success();
}

} // namespace

Status createParentDirectories(std::string_view utf8Path)
{
    if (const auto status = validateUtf8Path(utf8Path); !status)
    {
        return status;
    }
    std::error_code errorCode;
    const auto path = Detail::pathFromUtf8Bytes(utf8Path);
    const auto parent = path.parent_path();
    if (parent.empty())
    {
        return success();
    }
    std::filesystem::create_directories(parent, errorCode);
    if (errorCode)
    {
        return failure(makeIoError("failed to create parent directories", errorCode));
    }
    return success();
}

Status writeFile(std::string_view utf8Path, std::span<const std::byte> bytes, WriteFileConfig config)
{
    return writeFileParts(utf8Path, {&bytes, 1}, config);
}

Status writeFileParts(std::string_view utf8Path, std::span<const std::span<const std::byte>> parts,
                      WriteFileConfig config)
try
{
    if (const auto status = validateUtf8Path(utf8Path); !status)
    {
        return status;
    }

    std::error_code errorCode;
    const auto finalPath = Detail::pathFromUtf8Bytes(utf8Path);
    if (config.createParents)
    {
        if (const auto status = createParentDirectories(utf8Path); !status)
        {
            return status;
        }
    }

    bool ownsTempFile = false;
    const auto writeTo = [&](const std::filesystem::path& path) -> Status {
        const auto exclusive = config.atomicReplace ? std::ios::noreplace : std::ios::openmode{};
        std::ofstream output(path, std::ios::binary | std::ios::trunc | exclusive);
        if (!output)
        {
            return failure(CoreErrorCode::Io, "failed to open file for writing");
        }
        ownsTempFile = config.atomicReplace;
        for (auto bytes : parts)
        {
            while (!bytes.empty())
            {
                const auto count = (std::min)(bytes.size(), static_cast<usize>((std::numeric_limits<std::streamsize>::max)()));
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
                if (!output) return failure(CoreErrorCode::Io, "failed to write complete file contents");
                bytes = bytes.subspan(count);
            }
        }
        output.flush();
        if (!output)
        {
            return failure(CoreErrorCode::Io, "failed to flush file contents");
        }
        output.close();
        if (!output) return failure(CoreErrorCode::Io, "failed to close written file");
        return success();
    };

    if (!config.atomicReplace)
    {
        return writeTo(finalPath);
    }

    const auto parent = finalPath.parent_path();
    const auto tempPath = (parent.empty() ? std::filesystem::path{} : parent) / makeTempFileName();
    const auto cleanup = makeScopeExit([&]() noexcept {
        // An exclusive-create failure must never remove another publisher's temporary file.
        if (ownsTempFile) std::filesystem::remove(tempPath, errorCode);
    });
    if (const auto status = writeTo(tempPath); !status)
    {
        return status;
    }

    if (auto status = replaceFileAtomically(tempPath, finalPath); !status)
    {
        return status;
    }
    ownsTempFile = false;
    return success();
}
catch (const std::bad_alloc&)
{
    return failure(CoreErrorCode::OutOfMemory, "file write allocation failed");
}
catch (const std::system_error& error)
{
    return failure(makeIoError("file write failed", error.code()));
}

} // namespace Tina::Core
