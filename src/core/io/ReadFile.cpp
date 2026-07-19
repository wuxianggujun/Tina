#include <tina/core/io/ReadFile.hpp>

#include <tina/core/text/Utf8.hpp>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

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

} // namespace

Result<std::pmr::vector<std::byte>> readFile(std::string_view utf8Path, ReadFileConfig config)
{
    if (config.memoryResource == nullptr || config.maxBytes == 0 || config.maxBytes > MaxReadFileBytes)
    {
        return failure(CoreErrorCode::InvalidArgument, "invalid read file config");
    }
    if (utf8Path.empty() || containsEmbeddedNul(utf8Path))
    {
        return failure(CoreErrorCode::InvalidArgument, "file path must be non-empty UTF-8 without embedded NUL");
    }
    if (!countStrictUtf8CodepointsWithoutNul(utf8Path))
    {
        return failure(CoreErrorCode::InvalidArgument, "file path is not strict UTF-8");
    }

    std::error_code errorCode;
    const std::filesystem::path path = std::filesystem::u8path(utf8Path);
    const auto status = std::filesystem::status(path, errorCode);
    if (errorCode)
    {
        if (errorCode == std::errc::no_such_file_or_directory)
        {
            Error error{CoreErrorCode::NotFound, "file not found"};
            error.setNativeCode(static_cast<i64>(errorCode.value()));
            return failure(std::move(error));
        }
        if (errorCode == std::errc::permission_denied)
        {
            Error error{CoreErrorCode::PermissionDenied, "file permission denied"};
            error.setNativeCode(static_cast<i64>(errorCode.value()));
            return failure(std::move(error));
        }
        return failure(makeIoError("failed to query file status", errorCode));
    }
    if (!std::filesystem::exists(status))
    {
        return failure(CoreErrorCode::NotFound, "file not found");
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return failure(CoreErrorCode::InvalidArgument, "path is not a regular file");
    }

    const auto fileSize = std::filesystem::file_size(path, errorCode);
    if (errorCode)
    {
        return failure(makeIoError("failed to query file size", errorCode));
    }
    if (fileSize > config.maxBytes)
    {
        return failure(CoreErrorCode::CapacityExceeded, "file exceeds maxBytes limit");
    }
    if (fileSize > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
    {
        return failure(CoreErrorCode::CapacityExceeded, "file size exceeds addressable size");
    }

    const auto byteCount = static_cast<std::size_t>(fileSize);
    std::pmr::vector<std::byte> bytes{config.memoryResource};
    try
    {
        bytes.resize(byteCount);
    } catch (const std::bad_alloc&)
    {
        return failure(CoreErrorCode::OutOfMemory, "file buffer allocation failed");
    }

    if (byteCount == 0U)
    {
        return bytes;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        Error error{CoreErrorCode::Io, "failed to open file for reading"};
        error.setNativeCode(static_cast<i64>(errno));
        return failure(std::move(error));
    }
    input.read(static_cast<char*>(static_cast<void*>(bytes.data())), static_cast<std::streamsize>(byteCount));
    if (!input || static_cast<std::size_t>(input.gcount()) != byteCount)
    {
        return failure(CoreErrorCode::Io, "failed to read complete file contents");
    }
    return bytes;
}

} // namespace Tina::Core
