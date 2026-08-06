#include <tina/core/io/WriteFile.hpp>

#include "Utf8Path.hpp"

#include <tina/core/text/Utf8.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
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
    std::mt19937_64 rng{static_cast<std::uint64_t>(now) ^ 0x9E3779B97F4A7C15ULL};
    const auto token = rng();
    return ".tina_write_" + std::to_string(token) + ".tmp";
}

[[nodiscard]] Status replaceFileAtomically(const std::filesystem::path& tempPath,
                                           const std::filesystem::path& finalPath)
{
#if defined(_WIN32)
    if (::MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        const std::error_code errorCode{static_cast<int>(::GetLastError()), std::system_category()};
        return failure(makeIoError("failed to atomically replace target file", errorCode));
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

    const auto writeTo = [&](const std::filesystem::path& path) -> Status {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return failure(CoreErrorCode::Io, "failed to open file for writing");
        }
        if (!bytes.empty())
        {
            output.write(static_cast<const char*>(static_cast<const void*>(bytes.data())),
                         static_cast<std::streamsize>(bytes.size()));
            if (!output)
            {
                return failure(CoreErrorCode::Io, "failed to write complete file contents");
            }
        }
        output.flush();
        if (!output)
        {
            return failure(CoreErrorCode::Io, "failed to flush file contents");
        }
        return success();
    };

    if (!config.atomicReplace)
    {
        return writeTo(finalPath);
    }

    const auto parent = finalPath.parent_path();
    const auto tempPath = (parent.empty() ? std::filesystem::path{} : parent) / makeTempFileName();
    if (const auto status = writeTo(tempPath); !status)
    {
        std::filesystem::remove(tempPath, errorCode);
        return status;
    }

    if (auto status = replaceFileAtomically(tempPath, finalPath); !status)
    {
        std::filesystem::remove(tempPath, errorCode);
        return status;
    }
    return success();
}

} // namespace Tina::Core
