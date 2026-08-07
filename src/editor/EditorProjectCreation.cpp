#include <tina/editor/EditorProjectCreation.hpp>

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <sys/stat.h>
#endif

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::Error makeFilesystemError(std::string_view message,
                                              const std::error_code& nativeCode)
{
    Core::ErrorCode code = Core::CoreErrorCode::Io;
    if (nativeCode == std::errc::no_such_file_or_directory)
    {
        code = Core::CoreErrorCode::NotFound;
    }
    else if (nativeCode == std::errc::file_exists)
    {
        code = Core::CoreErrorCode::AlreadyExists;
    }
    else if (nativeCode == std::errc::permission_denied)
    {
        code = Core::CoreErrorCode::PermissionDenied;
    }

    Core::Error error{code, message};
    if (nativeCode)
    {
        error.setNativeCode(static_cast<Core::i64>(nativeCode.value()));
    }
    return error;
}

[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view pathUtf8)
{
    const auto* first = reinterpret_cast<const char8_t*>(pathUtf8.data());
    return std::filesystem::path{std::u8string(first, first + pathUtf8.size())};
}

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    if (leftText.size() != rightText.size() ||
        leftText.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    return ::CompareStringOrdinal(leftText.data(), static_cast<int>(leftText.size()),
                                  rightText.data(), static_cast<int>(rightText.size()),
                                  TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                          const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart)
    {
        if (candidatePart == candidate.end() ||
            !pathComponentEquals(*candidatePart, *ancestorPart))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pathsEqual(const std::filesystem::path& left,
                              const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) &&
           pathIsSameOrDescendant(right, left);
}

struct DirectoryIdentity final {
#if defined(_WIN32)
    DWORD volumeSerialNumber = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
#elif defined(__linux__)
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
#else
    std::filesystem::file_time_type creationStamp{};
#endif
};

[[nodiscard]] Core::Result<DirectoryIdentity>
captureDirectoryIdentity(const std::filesystem::path& path)
{
#if defined(_WIN32)
    HANDLE directory = ::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
    {
        return Core::failure(makeFilesystemError(
            "Failed to open Editor project directory for identity validation",
            std::error_code(static_cast<int>(::GetLastError()), std::system_category())));
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(directory, &information))
    {
        const DWORD nativeCode = ::GetLastError();
        ::CloseHandle(directory);
        return Core::failure(makeFilesystemError(
            "Failed to read Editor project directory identity",
            std::error_code(static_cast<int>(nativeCode), std::system_category())));
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
    {
        ::CloseHandle(directory);
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor project path identity is not a directory");
    }
    // Read the reparse attribute from the same handle used for identity. This
    // closes the check-then-open window in which a directory could be swapped
    // for a junction between two independent Win32 calls.
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
    {
        ::CloseHandle(directory);
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor project directory must not be a reparse point");
    }
    ::CloseHandle(directory);
    return DirectoryIdentity{
        .volumeSerialNumber = information.dwVolumeSerialNumber,
        .fileIndexHigh = information.nFileIndexHigh,
        .fileIndexLow = information.nFileIndexLow,
    };
#elif defined(__linux__)
    struct stat information {};
    if (::lstat(path.c_str(), &information) != 0)
    {
        return Core::failure(makeFilesystemError(
            "Failed to read Editor project directory identity",
            std::error_code(errno, std::generic_category())));
    }
    if (!S_ISDIR(information.st_mode) || S_ISLNK(information.st_mode))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor project directory identity is not a physical directory");
    }
    return DirectoryIdentity{
        .device = static_cast<std::uintmax_t>(information.st_dev),
        .inode = static_cast<std::uintmax_t>(information.st_ino),
    };
#else
    std::error_code errorCode;
    const auto stamp = std::filesystem::last_write_time(path, errorCode);
    if (errorCode)
    {
        return Core::failure(makeFilesystemError(
            "Failed to read Editor project directory identity", errorCode));
    }
    return DirectoryIdentity{.creationStamp = stamp};
#endif
}

[[nodiscard]] bool matchesDirectoryIdentity(const std::filesystem::path& path,
                                            const DirectoryIdentity& expected) noexcept
{
    try
    {
        auto current = captureDirectoryIdentity(path);
        if (!current)
        {
            return false;
        }
#if defined(_WIN32)
        return current->volumeSerialNumber == expected.volumeSerialNumber &&
               current->fileIndexHigh == expected.fileIndexHigh &&
               current->fileIndexLow == expected.fileIndexLow;
#elif defined(__linux__)
        return current->device == expected.device && current->inode == expected.inode;
#else
        return current->creationStamp == expected.creationStamp;
#endif
    }
    catch (...)
    {
        // Rollback is noexcept and must never terminate the process if a
        // filesystem implementation reports an allocation or conversion error.
        return false;
    }
}

[[nodiscard]] Core::Status validateDirectoryName(std::string_view directoryUtf8,
                                                 std::string_view requestField)
{
    if (directoryUtf8.empty() || !Core::isStrictUtf8WithoutNul(directoryUtf8))
    {
        Core::Error error{Core::CoreErrorCode::InvalidArgument,
                          "Editor project directory name must be strict UTF-8 without NUL"};
        error.addContext("CreateNewEditorProject", requestField);
        return Core::failure(std::move(error));
    }

    const auto path = pathFromUtf8(directoryUtf8);
    if (path.is_absolute() || path.has_root_path() || path != path.filename() || path == "." ||
        path == "..")
    {
        Core::Error error{Core::CoreErrorCode::InvalidArgument,
                          "Editor project directory name must be one relative path component"};
        error.addContext("CreateNewEditorProject", requestField);
        return Core::failure(std::move(error));
    }
#if defined(_WIN32)
    const auto nativeName = path.native();
    if (nativeName.empty() || nativeName.back() == L'.' || nativeName.back() == L' ')
    {
        Core::Error error{Core::CoreErrorCode::InvalidArgument,
                          "Editor project directory name is not a valid Windows component"};
        error.addContext("CreateNewEditorProject", requestField);
        return Core::failure(std::move(error));
    }
    for (const wchar_t character : nativeName)
    {
        if (character < 0x20 || character == L'<' || character == L'>' || character == L':' ||
            character == L'"' || character == L'|' || character == L'?' || character == L'*')
        {
            Core::Error error{Core::CoreErrorCode::InvalidArgument,
                              "Editor project directory name is not a valid Windows component"};
            error.addContext("CreateNewEditorProject", requestField);
            return Core::failure(std::move(error));
        }
    }
    const auto deviceNameEnd = nativeName.find(L'.');
    const std::wstring_view deviceName{nativeName.data(),
                                       deviceNameEnd == std::wstring::npos ? nativeName.size()
                                                                            : deviceNameEnd};
    const auto equalsDevice = [&deviceName](std::wstring_view candidate) noexcept {
        return deviceName.size() == candidate.size() &&
               ::CompareStringOrdinal(deviceName.data(), static_cast<int>(deviceName.size()),
                                      candidate.data(), static_cast<int>(candidate.size()),
                                      TRUE) == CSTR_EQUAL;
    };
    const auto hasDevicePrefix = [&deviceName](std::wstring_view candidate) noexcept {
        return deviceName.size() >= candidate.size() &&
               ::CompareStringOrdinal(deviceName.data(), static_cast<int>(candidate.size()),
                                      candidate.data(), static_cast<int>(candidate.size()),
                                      TRUE) == CSTR_EQUAL;
    };
    if (equalsDevice(L"CON") || equalsDevice(L"PRN") || equalsDevice(L"AUX") ||
        equalsDevice(L"NUL") ||
        (deviceName.size() == 4U &&
         (hasDevicePrefix(L"COM") || hasDevicePrefix(L"LPT")) &&
         deviceName[3] >= L'1' && deviceName[3] <= L'9'))
    {
        Core::Error error{Core::CoreErrorCode::InvalidArgument,
                          "Editor project directory name is a reserved Windows device name"};
        error.addContext("CreateNewEditorProject", requestField);
        return Core::failure(std::move(error));
    }
#endif
    return Core::success();
}

class DirectoryCreationRollback final {
public:
    DirectoryCreationRollback(std::filesystem::path projectRoot,
                              std::filesystem::path sourceRoot,
                              std::filesystem::path catalogRoot) noexcept
        : m_projectRoot(std::move(projectRoot)), m_sourceRoot(std::move(sourceRoot)),
          m_catalogRoot(std::move(catalogRoot))
    {
    }

    DirectoryCreationRollback(const DirectoryCreationRollback&) = delete;
    DirectoryCreationRollback& operator=(const DirectoryCreationRollback&) = delete;

    ~DirectoryCreationRollback() noexcept
    {
        if (m_committed)
        {
            return;
        }

        std::error_code ignored;
        if (m_catalogIdentity.has_value() &&
            matchesDirectoryIdentity(m_catalogRoot, *m_catalogIdentity))
        {
            std::filesystem::remove(m_catalogRoot, ignored);
        }
        if (m_sourceIdentity.has_value() &&
            matchesDirectoryIdentity(m_sourceRoot, *m_sourceIdentity))
        {
            ignored.clear();
            std::filesystem::remove(m_sourceRoot, ignored);
        }
        if (m_projectIdentity.has_value() &&
            matchesDirectoryIdentity(m_projectRoot, *m_projectIdentity))
        {
            ignored.clear();
            std::filesystem::remove(m_projectRoot, ignored);
        }
    }

    void setProjectCreated(DirectoryIdentity identity) noexcept
    {
        m_projectIdentity = identity;
    }

    void setSourceCreated(DirectoryIdentity identity) noexcept
    {
        m_sourceIdentity = identity;
    }

    void setCatalogCreated(DirectoryIdentity identity) noexcept
    {
        m_catalogIdentity = identity;
    }

    void commit() noexcept { m_committed = true; }

private:
    std::filesystem::path m_projectRoot{};
    std::filesystem::path m_sourceRoot{};
    std::filesystem::path m_catalogRoot{};
    std::optional<DirectoryIdentity> m_projectIdentity{};
    std::optional<DirectoryIdentity> m_sourceIdentity{};
    std::optional<DirectoryIdentity> m_catalogIdentity{};
    bool m_committed = false;
};

[[nodiscard]] Core::Status createOwnedDirectory(const std::filesystem::path& path,
                                                std::string_view failureMessage)
{
    std::error_code errorCode;
    if (std::filesystem::create_directory(path, errorCode))
    {
        return Core::success();
    }
    if (errorCode)
    {
        return Core::failure(makeFilesystemError(failureMessage, errorCode));
    }
    return Core::failure(Core::CoreErrorCode::AlreadyExists,
                         "Editor project directory appeared during creation");
}

[[nodiscard]] Core::Result<DirectoryIdentity>
validatePhysicalDirectory(const std::filesystem::path& path,
                          std::string_view reparseMessage)
{
    auto identity = captureDirectoryIdentity(path);
    if (!identity)
    {
        // Preserve the call-site-specific explanation for a reparse rejection;
        // other failures retain their native error and context.
        if (identity.error().code == Core::CoreErrorCode::InvalidArgument)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument, reparseMessage);
        }
        return Core::failure(std::move(identity.error()));
    }
    return identity;
}

[[nodiscard]] Core::Status validateFinalContainment(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& catalogRoot)
{
    std::error_code errorCode;
    const auto physicalProject = std::filesystem::weakly_canonical(projectRoot, errorCode);
    if (errorCode)
    {
        return Core::failure(makeFilesystemError(
            "Failed to resolve the physical Editor project root", errorCode));
    }
    const auto physicalSource = std::filesystem::weakly_canonical(sourceRoot, errorCode);
    if (errorCode)
    {
        return Core::failure(makeFilesystemError(
            "Failed to resolve the physical Editor source root", errorCode));
    }
    const auto physicalCatalog = std::filesystem::weakly_canonical(catalogRoot, errorCode);
    if (errorCode)
    {
        return Core::failure(makeFilesystemError(
            "Failed to resolve the physical Cooked Catalog root", errorCode));
    }
    if (!pathIsSameOrDescendant(physicalSource, physicalProject) ||
        !pathIsSameOrDescendant(physicalCatalog, physicalProject) ||
        pathsEqual(physicalSource, physicalProject) ||
        pathsEqual(physicalCatalog, physicalProject) ||
        pathsEqual(physicalSource, physicalCatalog) ||
        pathIsSameOrDescendant(physicalSource, physicalCatalog) ||
        pathIsSameOrDescendant(physicalCatalog, physicalSource))
    {
        return Core::failure(Core::CoreErrorCode::PermissionDenied,
                             "Editor project directories escaped the physical project root");
    }
    return Core::success();
}

} // namespace

Core::Result<EditorProjectWorkspace>
CreateNewEditorProject(EditorProjectCreationRequest request)
{
    try
    {
        if (request.projectRootUtf8.empty() ||
            !Core::isStrictUtf8WithoutNul(request.projectRootUtf8))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor project root must be strict UTF-8 without NUL");
        }
        if (auto status = validateDirectoryName(request.sourceDirectoryUtf8,
                                                "sourceDirectoryUtf8");
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateDirectoryName(request.cookedCatalogDirectoryUtf8,
                                                "cookedCatalogDirectoryUtf8");
            !status)
        {
            return Core::failure(std::move(status.error()));
        }

        if (pathComponentEquals(pathFromUtf8(request.sourceDirectoryUtf8),
                                pathFromUtf8(request.cookedCatalogDirectoryUtf8)))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor source and Cooked Catalog directory names must differ");
        }

        const auto requestedRoot = pathFromUtf8(request.projectRootUtf8);
        const auto sourceRoot = requestedRoot / pathFromUtf8(request.sourceDirectoryUtf8);
        const auto catalogRoot = requestedRoot /
                                 pathFromUtf8(request.cookedCatalogDirectoryUtf8);
        const auto sourceRootUtf8 = sourceRoot.generic_u8string();
        const auto catalogRootUtf8 = catalogRoot.generic_u8string();

        auto workspace = EditorProjectWorkspace::Create(
            EditorProjectWorkspaceDesc{
                .projectRootUtf8 = request.projectRootUtf8,
                .sourceRootUtf8 = std::string_view{
                    reinterpret_cast<const char*>(sourceRootUtf8.data()), sourceRootUtf8.size()},
                .cookedCatalogRootUtf8 = std::string_view{
                    reinterpret_cast<const char*>(catalogRootUtf8.data()), catalogRootUtf8.size()},
                .targetPlatform = request.targetPlatform,
            },
            request.workspaceConfig);
        if (!workspace)
        {
            return Core::failure(std::move(workspace.error())
                                     .withContext("CreateNewEditorProject", "workspace"));
        }

        const auto projectPath = pathFromUtf8(workspace->projectRootUtf8());
        const auto sourcePath = pathFromUtf8(workspace->sourceRootUtf8());
        const auto catalogPath = pathFromUtf8(workspace->cookedCatalogRootUtf8());

        std::error_code statusError;
        const auto rootStatus = std::filesystem::symlink_status(projectPath, statusError);
        if (statusError && statusError != std::errc::no_such_file_or_directory)
        {
            return Core::failure(makeFilesystemError(
                "Failed to inspect the Editor project root", statusError));
        }

        const bool rootExists = std::filesystem::exists(rootStatus);
        std::optional<DirectoryIdentity> rootIdentity;
        if (rootExists)
        {
            if (std::filesystem::is_symlink(rootStatus) ||
                !std::filesystem::is_directory(rootStatus))
            {
                return Core::failure(Core::CoreErrorCode::AlreadyExists,
                                     "Editor project root already exists and is not a dedicated directory");
            }

            statusError.clear();
            const bool empty = std::filesystem::is_empty(projectPath, statusError);
            if (statusError)
            {
                return Core::failure(makeFilesystemError(
                    "Failed to inspect Editor project root contents", statusError));
            }
            if (!empty)
            {
                return Core::failure(Core::CoreErrorCode::AlreadyExists,
                                     "Editor project root already exists and is not empty");
            }
            auto identity = validatePhysicalDirectory(
                projectPath,
                "Editor project root must not be a symlink, junction, or reparse point");
            if (!identity)
            {
                return Core::failure(std::move(identity.error()));
            }
            rootIdentity = *identity;
        }

        DirectoryCreationRollback rollback{projectPath, sourcePath, catalogPath};
        if (!rootExists)
        {
            if (auto status = createOwnedDirectory(projectPath,
                                                   "Failed to create Editor project root");
                !status)
            {
                return Core::failure(std::move(status.error()));
            }
            auto identity = validatePhysicalDirectory(
                projectPath,
                "Created Editor project root became a symlink, junction, or reparse point");
            if (!identity)
            {
                return Core::failure(std::move(identity.error()));
            }
            rootIdentity = *identity;
            rollback.setProjectCreated(*identity);
        }

        if (auto status = createOwnedDirectory(sourcePath,
                                               "Failed to create Editor source directory");
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        auto sourceIdentity = validatePhysicalDirectory(
            sourcePath,
            "Created Editor source root became a symlink, junction, or reparse point");
        if (!sourceIdentity)
        {
            return Core::failure(std::move(sourceIdentity.error()));
        }
        rollback.setSourceCreated(*sourceIdentity);

        if (auto status = createOwnedDirectory(catalogPath,
                                               "Failed to create Cooked Catalog directory");
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        auto catalogIdentity = validatePhysicalDirectory(
            catalogPath,
            "Created Cooked Catalog root became a symlink, junction, or reparse point");
        if (!catalogIdentity)
        {
            return Core::failure(std::move(catalogIdentity.error()));
        }
        rollback.setCatalogCreated(*catalogIdentity);

        if (!rootIdentity.has_value() ||
            !matchesDirectoryIdentity(projectPath, *rootIdentity) ||
            !sourceIdentity || !matchesDirectoryIdentity(sourcePath, *sourceIdentity) ||
            !catalogIdentity || !matchesDirectoryIdentity(catalogPath, *catalogIdentity))
        {
            return Core::failure(Core::CoreErrorCode::Io,
                                 "Editor project directory identity changed during creation");
        }
        if (auto status = validateFinalContainment(projectPath, sourcePath, catalogPath);
            !status)
        {
            return Core::failure(std::move(status.error()));
        }

        rollback.commit();
        return std::move(*workspace);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor project creation allocation failed");
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return Core::failure(makeFilesystemError(
            "Editor project creation filesystem operation failed", exception.code()));
    }
}

} // namespace Tina::Editor
