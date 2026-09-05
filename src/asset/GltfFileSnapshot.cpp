#include "GltfFileSnapshot.hpp"

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fstream>
#endif

namespace Tina::Asset::GltfDetail {

std::filesystem::path snapshotContainmentPath(std::filesystem::path path)
{
#if defined(_WIN32)
    if (path.empty())
    {
        return path;
    }
    const std::wstring& native = path.native();
    if (native.starts_with(L"\\\\?\\"))
    {
        return path;
    }
    if (native.starts_with(L"\\\\"))
    {
        return std::filesystem::path{L"\\\\?\\UNC\\" + native.substr(2)};
    }
    return std::filesystem::path{L"\\\\?\\" + native};
#else
    return path;
#endif
}

namespace {

[[nodiscard]] bool samePathComponent(const std::filesystem::path& left,
                                     const std::filesystem::path& right)
{
#if defined(_WIN32)
    const std::wstring& leftText = left.native();
    const std::wstring& rightText = right.native();
    if (leftText.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        rightText.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    // Final handle paths carry the filesystem's canonical component spelling. Exact comparison
    // also preserves containment on NTFS directories with case sensitivity enabled.
    return CompareStringOrdinal(leftText.data(), static_cast<int>(leftText.size()),
                                rightText.data(), static_cast<int>(rightText.size()), FALSE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

[[nodiscard]] bool isStrictlyContained(const std::filesystem::path& root,
                                       const std::filesystem::path& candidate)
{
    const std::filesystem::path normalizedRoot = root.lexically_normal();
    const std::filesystem::path normalizedCandidate = candidate.lexically_normal();
    auto rootIt = normalizedRoot.begin();
    auto candidateIt = normalizedCandidate.begin();
    while (rootIt != normalizedRoot.end())
    {
        if (candidateIt == normalizedCandidate.end() || !samePathComponent(*rootIt, *candidateIt))
        {
            return false;
        }
        ++rootIt;
        ++candidateIt;
    }
    return candidateIt != normalizedCandidate.end();
}

[[nodiscard]] Core::Result<std::size_t> bytesToRead(std::uint64_t fileSize,
                                                    std::uint64_t maxFileBytes,
                                                    std::uint64_t requestedBytes,
                                                    bool allowShorterFile)
{
    if ((fileSize == 0 && !(allowShorterFile && requestedBytes > 0)) ||
        fileSize > maxFileBytes ||
        (!allowShorterFile && requestedBytes > fileSize))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF source file size is outside the configured limit");
    }
    const std::uint64_t readBytes =
        requestedBytes == 0 ? fileSize : (std::min)(requestedBytes, fileSize);
    if (readBytes > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF source file size exceeds addressable memory");
    }
    return static_cast<std::size_t>(readBytes);
}

#if defined(_WIN32)

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE value) noexcept : m_value(value) {}
    ~UniqueHandle()
    {
        if (m_value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_value);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_value; }

private:
    HANDLE m_value = INVALID_HANDLE_VALUE;
};

[[nodiscard]] Core::Result<std::filesystem::path> finalPathForHandle(HANDLE handle)
{
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to resolve opened glTF source path");
    }
    std::wstring path(static_cast<std::size_t>(required) + 1U, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, path.data(),
                                                    static_cast<DWORD>(path.size()), flags);
    if (written == 0 || static_cast<std::size_t>(written) >= path.size())
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to read opened glTF source path");
    }
    path.resize(written);
    return std::filesystem::path{std::move(path)}.lexically_normal();
}

[[nodiscard]] Core::Result<FileSnapshot> readPlatformFileSnapshot(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path* containmentRoot,
    std::uint64_t maxFileBytes,
    std::uint64_t requestedBytes,
    bool allowShorterFile)
{
    UniqueHandle file{CreateFileW(requestedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE || GetFileType(file.get()) != FILE_TYPE_DISK)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to open regular glTF source file");
    }

    auto finalPath = finalPathForHandle(file.get());
    if (!finalPath)
    {
        return Core::failure(std::move(finalPath.error()));
    }
    if (containmentRoot != nullptr && !isStrictlyContained(*containmentRoot, *finalPath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "opened glTF external file escapes authoring root");
    }

    BY_HANDLE_FILE_INFORMATION initialInfo{};
    if (!GetFileInformationByHandle(file.get(), &initialInfo))
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to query glTF source file identity");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to query glTF source file size");
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(size.QuadPart);
    auto readSize = bytesToRead(fileSize, maxFileBytes, requestedBytes, allowShorterFile);
    if (!readSize)
    {
        return Core::failure(std::move(readSize.error()));
    }

    FileSnapshot result{.finalPath = std::move(*finalPath), .fileSize = fileSize};
    result.bytes.resize(*readSize);
    std::size_t offset = 0;
    while (offset < result.bytes.size())
    {
        const std::size_t remaining = result.bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>((std::min)(
            remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD read = 0;
        if (!ReadFile(file.get(), result.bytes.data() + offset, chunk, &read, nullptr) || read != chunk)
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                                 "failed to read complete glTF source file snapshot");
        }
        offset += read;
    }
    BY_HANDLE_FILE_INFORMATION finalInfo{};
    if (!GetFileInformationByHandle(file.get(), &finalInfo) ||
        initialInfo.dwVolumeSerialNumber != finalInfo.dwVolumeSerialNumber ||
        initialInfo.nFileIndexHigh != finalInfo.nFileIndexHigh ||
        initialInfo.nFileIndexLow != finalInfo.nFileIndexLow ||
        initialInfo.nFileSizeHigh != finalInfo.nFileSizeHigh ||
        initialInfo.nFileSizeLow != finalInfo.nFileSizeLow ||
        initialInfo.ftLastWriteTime.dwHighDateTime != finalInfo.ftLastWriteTime.dwHighDateTime ||
        initialInfo.ftLastWriteTime.dwLowDateTime != finalInfo.ftLastWriteTime.dwLowDateTime)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "glTF source file changed while snapshot was read");
    }
    return result;
}

#elif defined(__linux__)

class UniqueFd final {
public:
    explicit UniqueFd(int value) noexcept : m_value(value) {}
    ~UniqueFd()
    {
        if (m_value >= 0)
        {
            ::close(m_value);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    [[nodiscard]] int get() const noexcept { return m_value; }

private:
    int m_value = -1;
};

[[nodiscard]] Core::Result<std::filesystem::path> finalPathForFd(int fd)
{
    const std::string fdPath = "/proc/self/fd/" + std::to_string(fd);
    std::vector<char> path(256U);
    for (;;)
    {
        const ssize_t written = ::readlink(fdPath.c_str(), path.data(), path.size());
        if (written < 0)
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                                 "failed to resolve opened glTF source path");
        }
        if (static_cast<std::size_t>(written) < path.size())
        {
            const std::string_view finalText{path.data(), static_cast<std::size_t>(written)};
            if (finalText.ends_with(" (deleted)"))
            {
                return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                                     "opened glTF source file was replaced during cook");
            }
            return std::filesystem::path{finalText}.lexically_normal();
        }
        if (path.size() > 1024U * 1024U)
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                                 "opened glTF source path is too long");
        }
        path.resize(path.size() * 2U);
    }
}

[[nodiscard]] Core::Result<FileSnapshot> readPlatformFileSnapshot(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path* containmentRoot,
    std::uint64_t maxFileBytes,
    std::uint64_t requestedBytes,
    bool allowShorterFile)
{
    UniqueFd file{::open(requestedPath.c_str(), O_RDONLY | O_CLOEXEC)};
    if (file.get() < 0)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to open regular glTF source file");
    }

    struct stat info {};
    if (::fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "opened glTF source is not a regular file");
    }
    auto finalPath = finalPathForFd(file.get());
    if (!finalPath)
    {
        return Core::failure(std::move(finalPath.error()));
    }
    if (containmentRoot != nullptr && !isStrictlyContained(*containmentRoot, *finalPath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "opened glTF external file escapes authoring root");
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(info.st_size);
    auto readSize = bytesToRead(fileSize, maxFileBytes, requestedBytes, allowShorterFile);
    if (!readSize)
    {
        return Core::failure(std::move(readSize.error()));
    }

    FileSnapshot result{.finalPath = std::move(*finalPath), .fileSize = fileSize};
    result.bytes.resize(*readSize);
    std::size_t offset = 0;
    while (offset < result.bytes.size())
    {
        const ssize_t read = ::read(file.get(), result.bytes.data() + offset,
                                    result.bytes.size() - offset);
        if (read < 0 && errno == EINTR)
        {
            continue;
        }
        if (read <= 0)
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                                 "failed to read complete glTF source file snapshot");
        }
        offset += static_cast<std::size_t>(read);
    }
    struct stat finalInfo {};
    if (::fstat(file.get(), &finalInfo) != 0 || info.st_dev != finalInfo.st_dev ||
        info.st_ino != finalInfo.st_ino || info.st_size != finalInfo.st_size ||
        info.st_mtim.tv_sec != finalInfo.st_mtim.tv_sec ||
        info.st_mtim.tv_nsec != finalInfo.st_mtim.tv_nsec ||
        info.st_ctim.tv_sec != finalInfo.st_ctim.tv_sec ||
        info.st_ctim.tv_nsec != finalInfo.st_ctim.tv_nsec)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "glTF source file changed while snapshot was read");
    }
    return result;
}

#else

[[nodiscard]] Core::Result<FileSnapshot> readPlatformFileSnapshot(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path* containmentRoot,
    std::uint64_t maxFileBytes,
    std::uint64_t requestedBytes,
    bool allowShorterFile)
{
    std::error_code ec;
    const std::filesystem::path finalPath = std::filesystem::canonical(requestedPath, ec);
    if (ec || (containmentRoot != nullptr && !isStrictlyContained(*containmentRoot, finalPath)))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF source path is outside authoring root");
    }
    const std::uint64_t fileSize = std::filesystem::file_size(finalPath, ec);
    if (ec)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to query glTF source file size");
    }
    auto readSize = bytesToRead(fileSize, maxFileBytes, requestedBytes, allowShorterFile);
    if (!readSize)
    {
        return Core::failure(std::move(readSize.error()));
    }
    FileSnapshot result{.finalPath = finalPath, .fileSize = fileSize};
    result.bytes.resize(*readSize);
    std::ifstream input(finalPath, std::ios::binary);
    input.read(reinterpret_cast<char*>(result.bytes.data()),
               static_cast<std::streamsize>(result.bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != result.bytes.size())
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to read complete glTF source file snapshot");
    }
    return result;
}

#endif

} // namespace

Core::Result<FileSnapshot> readFileSnapshot(const std::filesystem::path& requestedPath,
                                            const std::filesystem::path* containmentRoot,
                                            std::uint64_t maxFileBytes,
                                            std::uint64_t requestedBytes,
                                            bool allowShorterFile) noexcept
{
    if (requestedPath.empty() || maxFileBytes == 0 || requestedBytes > maxFileBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "invalid glTF source snapshot request");
    }
    try
    {
        return readPlatformFileSnapshot(requestedPath, containmentRoot, maxFileBytes,
                                        requestedBytes, allowShorterFile);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "glTF source snapshot allocation failed");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "glTF source snapshot filesystem operation failed");
    }
    catch (...)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "unexpected glTF source snapshot failure");
    }
}

} // namespace Tina::Asset::GltfDetail
