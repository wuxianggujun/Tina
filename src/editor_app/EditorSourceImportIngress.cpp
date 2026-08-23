#include "EditorSourceImportIngress.hpp"

#include "EditorSourceImportSelection.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Tina::EditorApp::Detail {
namespace {

constexpr Core::u32 MaximumCollisionSuffix = 100'000U;
constexpr std::size_t FileComparisonBufferSize = 64U * 1024U;

struct PlannedCopy final {
    std::filesystem::path source{};
    std::filesystem::path destination{};
    std::string destinationUtf8{};
};

struct ContentVerification final {
    std::filesystem::path source{};
    std::filesystem::path destination{};
};

struct ExternalSourceMapping final {
    std::filesystem::path source{};
    std::filesystem::path destination{};
};

[[nodiscard]] Core::Status checkStopped(const std::stop_token stopToken)
{
    if (stopToken.stop_requested()) {
        return Core::failure(Asset::AssetErrorCode::SourceImportCancelled,
                             "Editor source import file transaction was cancelled");
    }
    return Core::success();
}

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    if (leftText.size() != rightText.size() ||
        leftText.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
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
         ++ancestorPart, ++candidatePart) {
        if (candidatePart == candidate.end() ||
            !pathComponentEquals(*candidatePart, *ancestorPart)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pathsReferToSameLocation(const std::filesystem::path& left,
                                            const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) && pathIsSameOrDescendant(right, left);
}

[[nodiscard]] Core::Error ioError(std::string_view message,
                                  const std::filesystem::path& path,
                                  const std::error_code& nativeError)
{
    Core::Error error{Core::CoreErrorCode::Io, message};
    if (nativeError) {
        error.setNativeCode(nativeError.value());
    }
    error.addContext("path", pathToUtf8(path));
    return error;
}

[[nodiscard]] Core::Status rejectWindowsReparsePoint(
    const std::filesystem::path& path,
    std::string_view message)
{
#if defined(_WIN32)
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return Core::failure(ioError(message, path,
                                     std::error_code{static_cast<int>(::GetLastError()),
                                                     std::system_category()}));
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        Core::Error error{Core::CoreErrorCode::PermissionDenied, message};
        error.addContext("path", pathToUtf8(path));
        return Core::failure(std::move(error));
    }
#else
    static_cast<void>(path);
    static_cast<void>(message);
#endif
    return Core::success();
}

[[nodiscard]] Core::Status validateRegularPhysicalFile(
    const std::filesystem::path& path,
    std::string_view message)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError) {
        return Core::failure(ioError(message, path, statusError));
    }
    if (!std::filesystem::is_regular_file(status)) {
        Core::Error error{Core::CoreErrorCode::InvalidArgument, message};
        error.addContext("path", pathToUtf8(path));
        return Core::failure(std::move(error));
    }
    return rejectWindowsReparsePoint(path, message);
}

[[nodiscard]] Core::Status validatePhysicalDirectory(
    const std::filesystem::path& path,
    std::string_view message)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError) {
        return Core::failure(ioError(message, path, statusError));
    }
    if (!std::filesystem::is_directory(status)) {
        Core::Error error{Core::CoreErrorCode::InvalidArgument, message};
        error.addContext("path", pathToUtf8(path));
        return Core::failure(std::move(error));
    }
    return rejectWindowsReparsePoint(path, message);
}

[[nodiscard]] Core::Result<bool> filesHaveSameContent(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::stop_token stopToken)
{
    if (const auto status = checkStopped(stopToken); !status) {
        return Core::failure(std::move(status.error()));
    }
    std::error_code sizeError;
    const std::uintmax_t leftSize = std::filesystem::file_size(left, sizeError);
    if (sizeError) {
        return Core::failure(ioError("Editor could not inspect an import source file size",
                                     left, sizeError));
    }
    const std::uintmax_t rightSize = std::filesystem::file_size(right, sizeError);
    if (sizeError) {
        return Core::failure(ioError("Editor could not inspect an import destination file size",
                                     right, sizeError));
    }
    if (leftSize != rightSize) {
        return false;
    }

    std::ifstream leftStream{left, std::ios::binary};
    if (!leftStream.is_open()) {
        return Core::failure(ioError("Editor could not open an import source file for comparison",
                                     left, {}));
    }
    std::ifstream rightStream{right, std::ios::binary};
    if (!rightStream.is_open()) {
        return Core::failure(ioError(
            "Editor could not open an import destination file for comparison", right, {}));
    }

    std::vector<char> comparisonStorage;
    try {
        comparisonStorage.resize(FileComparisonBufferSize * 2U);
    } catch (const std::bad_alloc&) {
        return Core::failure(
            Core::CoreErrorCode::OutOfMemory,
            "Editor could not allocate the import file comparison buffer");
    }
    std::span<char> leftBuffer{
        comparisonStorage.data(), FileComparisonBufferSize};
    std::span<char> rightBuffer{
        comparisonStorage.data() + FileComparisonBufferSize,
        FileComparisonBufferSize};
    for (;;) {
        if (const auto status = checkStopped(stopToken); !status) {
            return Core::failure(std::move(status.error()));
        }
        leftStream.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        const std::streamsize leftRead = leftStream.gcount();
        rightStream.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const std::streamsize rightRead = rightStream.gcount();
        if (leftStream.bad()) {
            return Core::failure(ioError("Editor could not read an import source file",
                                         left, {}));
        }
        if (rightStream.bad()) {
            return Core::failure(ioError("Editor could not read an import destination file",
                                         right, {}));
        }
        if (leftRead != rightRead ||
            !std::equal(leftBuffer.begin(),
                        leftBuffer.begin() + static_cast<std::ptrdiff_t>(leftRead),
                        rightBuffer.begin())) {
            return false;
        }
        if (leftRead == 0) {
            return true;
        }
    }
}

[[nodiscard]] Core::Result<bool> pathsReferToSamePhysicalFile(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    if (pathsReferToSameLocation(left, right)) {
        return true;
    }
    std::error_code equivalentError;
    const bool equivalent = std::filesystem::equivalent(left, right, equivalentError);
    if (equivalentError) {
        return Core::failure(ioError("Editor could not compare selected import files",
                                     right, equivalentError));
    }
    return equivalent;
}

[[nodiscard]] Core::Result<std::optional<std::filesystem::file_status>>
existingStatus(const std::filesystem::path& path)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError && statusError != std::errc::no_such_file_or_directory) {
        return Core::failure(ioError("Editor could not inspect an import destination",
                                     path, statusError));
    }
    if (statusError || !std::filesystem::exists(status)) {
        return std::optional<std::filesystem::file_status>{};
    }
    return std::optional<std::filesystem::file_status>{status};
}

[[nodiscard]] Core::Result<std::filesystem::path> collisionCandidate(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    Core::u32 suffix)
{
    std::filesystem::path fileName = source.filename();
    if (suffix > 1U) {
        fileName = source.stem();
        fileName += "_" + std::to_string(suffix);
        fileName += source.extension();
    }
    const auto candidate = (directory / fileName).lexically_normal();
    const std::string candidateUtf8 = pathToUtf8(candidate);
    if (candidateUtf8.empty() ||
        candidateUtf8.size() > AssetFormat::SourceImportWire::MaxPathBytes ||
        !Core::isStrictUtf8WithoutNul(candidateUtf8)) {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "Editor import destination path exceeds the bounded UTF-8 path limit");
    }
    return candidate;
}

[[nodiscard]] Core::Status validateOptionalDestinationDirectory(
    const std::filesystem::path& directory)
{
    auto status = existingStatus(directory);
    if (!status) {
        return Core::failure(std::move(status.error()));
    }
    if (!status->has_value()) {
        return Core::success();
    }
    return validatePhysicalDirectory(
        directory, "Editor import destination must be a physical directory");
}

[[nodiscard]] Core::Result<bool> ensureDestinationDirectory(
    const std::filesystem::path& directory)
{
    std::error_code createError;
    const bool created = std::filesystem::create_directory(directory, createError);
    if (createError) {
        return Core::failure(ioError("Editor could not create an import destination directory",
                                     directory, createError));
    }
    if (auto status = validatePhysicalDirectory(
            directory, "Editor import destination must be a physical directory");
        !status) {
        return Core::failure(std::move(status.error()));
    }
    return created;
}

} // namespace

EditorSourceImportIngress::~EditorSourceImportIngress() noexcept
{
    rollback();
}

EditorSourceImportIngress::EditorSourceImportIngress(
    EditorSourceImportIngress&& other) noexcept
    : projectPathsUtf8_(std::move(other.projectPathsUtf8_)),
      createdFilePathsUtf8_(std::move(other.createdFilePathsUtf8_)),
      createdDirectoryPathsUtf8_(std::move(other.createdDirectoryPathsUtf8_)),
      copiedFileCount_(other.copiedFileCount_),
      reusedFileCount_(other.reusedFileCount_),
      committed_(other.committed_)
{
    other.committed_ = true;
    other.copiedFileCount_ = 0;
    other.reusedFileCount_ = 0;
}

std::span<const std::string> EditorSourceImportIngress::projectPathsUtf8() const noexcept
{
    return projectPathsUtf8_;
}

Core::u32 EditorSourceImportIngress::copiedFileCount() const noexcept
{
    return copiedFileCount_;
}

Core::u32 EditorSourceImportIngress::reusedFileCount() const noexcept
{
    return reusedFileCount_;
}

void EditorSourceImportIngress::commit() noexcept
{
    committed_ = true;
    createdFilePathsUtf8_.clear();
    createdDirectoryPathsUtf8_.clear();
}

void EditorSourceImportIngress::rollback() noexcept
{
    if (committed_) {
        return;
    }
    for (auto file = createdFilePathsUtf8_.rbegin();
         file != createdFilePathsUtf8_.rend(); ++file) {
        try {
            std::error_code cleanupError;
            (void)std::filesystem::remove(
                std::filesystem::u8path(file->begin(), file->end()), cleanupError);
        } catch (...) {
        }
    }
    for (auto directory = createdDirectoryPathsUtf8_.rbegin();
         directory != createdDirectoryPathsUtf8_.rend(); ++directory) {
        try {
            std::error_code cleanupError;
            (void)std::filesystem::remove(
                std::filesystem::u8path(directory->begin(), directory->end()), cleanupError);
        } catch (...) {
        }
    }
    committed_ = true;
}

Core::Result<EditorSourceImportIngress>
prepareEditorSourceImportIngress(
    std::string_view sourceRootUtf8,
    std::span<const std::string> selectedPathsUtf8,
    Core::u32 maxSelectedPaths,
    std::stop_token stopToken,
    EditorSourceImportIngressProgress progress)
{
    if (maxSelectedPaths == 0U || maxSelectedPaths > EditorSourceImportUnitCapacity) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor import selection capacity is invalid");
    }
    if (selectedPathsUtf8.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor import selection must not be empty");
    }
    if (selectedPathsUtf8.size() > maxSelectedPaths) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Editor import selection capacity exceeded");
    }
    if (sourceRootUtf8.empty() || !Core::isStrictUtf8WithoutNul(sourceRootUtf8)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor project Source path must be strict UTF-8 without NUL");
    }
    if (const auto status = checkStopped(stopToken); !status) {
        return Core::failure(std::move(status.error()));
    }

    try {
        const auto sourceRootInput =
            std::filesystem::u8path(sourceRootUtf8.begin(), sourceRootUtf8.end());
        if (!sourceRootInput.is_absolute()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor project Source path must be absolute");
        }
        if (auto status = validatePhysicalDirectory(
                sourceRootInput, "Editor project Source path must be a physical directory");
            !status) {
            return Core::failure(std::move(status.error()));
        }
        std::error_code canonicalError;
        const auto sourceRoot =
            std::filesystem::weakly_canonical(sourceRootInput, canonicalError);
        if (canonicalError) {
            return Core::failure(ioError("Editor could not resolve the project Source directory",
                                         sourceRootInput, canonicalError));
        }

        const auto importedRoot = sourceRoot / "Imported";
        const auto imageRoot = importedRoot / "Images";
        const auto audioRoot = importedRoot / "Audio";

        EditorSourceImportIngress ingress;
        ingress.projectPathsUtf8_.reserve(selectedPathsUtf8.size());
        ingress.createdFilePathsUtf8_.reserve(selectedPathsUtf8.size());
        ingress.createdDirectoryPathsUtf8_.reserve(3U);

        std::vector<PlannedCopy> copies;
        std::vector<ContentVerification> verifications;
        std::vector<ExternalSourceMapping> externalMappings;
        copies.reserve(selectedPathsUtf8.size());
        verifications.reserve(selectedPathsUtf8.size());
        externalMappings.reserve(selectedPathsUtf8.size());

        bool requiresImageDirectory = false;
        bool requiresAudioDirectory = false;
        for (const std::string& selectedPathUtf8 : selectedPathsUtf8) {
            if (const auto status = checkStopped(stopToken); !status) {
                return Core::failure(std::move(status.error()));
            }
            if (selectedPathUtf8.empty() ||
                selectedPathUtf8.size() > AssetFormat::SourceImportWire::MaxPathBytes ||
                !Core::isStrictUtf8WithoutNul(selectedPathUtf8)) {
                return Core::failure(
                    Core::CoreErrorCode::InvalidArgument,
                    "Editor import path must be bounded strict UTF-8 without NUL");
            }
            const auto selectedInput = std::filesystem::u8path(
                selectedPathUtf8.begin(), selectedPathUtf8.end());
            if (!selectedInput.is_absolute()) {
                return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                     "Editor import path must be absolute");
            }
            if (auto status = validateRegularPhysicalFile(
                    selectedInput, "Editor import path must name an existing physical file");
                !status) {
                return Core::failure(std::move(status.error()));
            }
            const auto selected =
                std::filesystem::weakly_canonical(selectedInput, canonicalError);
            if (canonicalError) {
                return Core::failure(ioError("Editor could not resolve a selected import file",
                                             selectedInput, canonicalError));
            }
            auto kind = editorSourceImportUnitKindForPath(pathToUtf8(selected));
            if (!kind) {
                return Core::failure(std::move(kind.error()));
            }

            if (pathIsSameOrDescendant(selected, sourceRoot) &&
                !pathsReferToSameLocation(selected, sourceRoot)) {
                ingress.projectPathsUtf8_.push_back(pathToUtf8(selected.lexically_normal()));
                continue;
            }
            if (*kind != EditorSourceImportUnitKind::Texture &&
                *kind != EditorSourceImportUnitKind::Audio) {
                Core::Error error{
                    Core::CoreErrorCode::PermissionDenied,
                    "External recipe and glTF sources cannot be copied safely; place their complete "
                    "dependency set below the project's Source directory before importing"};
                error.addContext("sourcePath", pathToUtf8(selected));
                return Core::failure(std::move(error));
            }

            std::optional<std::filesystem::path> duplicateDestination;
            for (const auto& mapping : externalMappings) {
                auto sameFile = pathsReferToSamePhysicalFile(selected, mapping.source);
                if (!sameFile) {
                    return Core::failure(std::move(sameFile.error()));
                }
                if (*sameFile) {
                    duplicateDestination = mapping.destination;
                    break;
                }
            }
            if (duplicateDestination.has_value()) {
                ingress.projectPathsUtf8_.push_back(pathToUtf8(*duplicateDestination));
                continue;
            }

            const auto& destinationRoot =
                *kind == EditorSourceImportUnitKind::Texture ? imageRoot : audioRoot;
            requiresImageDirectory = requiresImageDirectory ||
                                     *kind == EditorSourceImportUnitKind::Texture;
            requiresAudioDirectory = requiresAudioDirectory ||
                                     *kind == EditorSourceImportUnitKind::Audio;

            std::optional<std::filesystem::path> destination;
            bool destinationNeedsCopy = false;
            for (Core::u32 suffix = 1U; suffix <= MaximumCollisionSuffix; ++suffix) {
                auto candidate = collisionCandidate(destinationRoot, selected, suffix);
                if (!candidate) {
                    return Core::failure(std::move(candidate.error()));
                }

                const auto planned = std::find_if(
                    copies.begin(), copies.end(), [&](const PlannedCopy& copy) {
                        return pathsReferToSameLocation(copy.destination, *candidate);
                    });
                if (planned != copies.end()) {
                    auto sameContent = filesHaveSameContent(
                        selected, planned->source, stopToken);
                    if (!sameContent) {
                        return Core::failure(std::move(sameContent.error()));
                    }
                    if (*sameContent) {
                        destination = planned->destination;
                        verifications.push_back({selected, planned->destination});
                        ++ingress.reusedFileCount_;
                        break;
                    }
                    continue;
                }

                auto candidateStatus = existingStatus(*candidate);
                if (!candidateStatus) {
                    return Core::failure(std::move(candidateStatus.error()));
                }
                if (!candidateStatus->has_value()) {
                    destination = *candidate;
                    destinationNeedsCopy = true;
                    break;
                }
                if (!std::filesystem::is_regular_file(**candidateStatus)) {
                    continue;
                }
                if (auto status = rejectWindowsReparsePoint(
                        *candidate, "Editor import destination must not be a reparse point");
                    !status) {
                    return Core::failure(std::move(status.error()));
                }
                auto sameContent = filesHaveSameContent(
                    selected, *candidate, stopToken);
                if (!sameContent) {
                    return Core::failure(std::move(sameContent.error()));
                }
                if (*sameContent) {
                    destination = *candidate;
                    verifications.push_back({selected, *candidate});
                    ++ingress.reusedFileCount_;
                    break;
                }
            }
            if (!destination.has_value()) {
                return Core::failure(
                    Core::CoreErrorCode::CapacityExceeded,
                    "Editor could not allocate a bounded non-conflicting import file name");
            }
            if (destinationNeedsCopy) {
                std::string destinationUtf8 = pathToUtf8(*destination);
                copies.push_back({selected, *destination, std::move(destinationUtf8)});
            }
            externalMappings.push_back({selected, *destination});
            ingress.projectPathsUtf8_.push_back(pathToUtf8(*destination));
        }

        if (requiresImageDirectory || requiresAudioDirectory) {
            if (auto status = validateOptionalDestinationDirectory(importedRoot); !status) {
                return Core::failure(std::move(status.error()));
            }
        }
        if (requiresImageDirectory) {
            if (auto status = validateOptionalDestinationDirectory(imageRoot); !status) {
                return Core::failure(std::move(status.error()));
            }
        }
        if (requiresAudioDirectory) {
            if (auto status = validateOptionalDestinationDirectory(audioRoot); !status) {
                return Core::failure(std::move(status.error()));
            }
        }

        std::string importedRootUtf8;
        std::string imageRootUtf8;
        std::string audioRootUtf8;
        if (!copies.empty()) {
            progress.notifyCopyingStarted();
            if (const auto status = checkStopped(stopToken); !status) {
                return Core::failure(std::move(status.error()));
            }
            importedRootUtf8 = pathToUtf8(importedRoot);
            if (requiresImageDirectory) {
                imageRootUtf8 = pathToUtf8(imageRoot);
            }
            if (requiresAudioDirectory) {
                audioRootUtf8 = pathToUtf8(audioRoot);
            }

            auto importedCreated = ensureDestinationDirectory(importedRoot);
            if (!importedCreated) {
                return Core::failure(std::move(importedCreated.error()));
            }
            if (*importedCreated) {
                ingress.createdDirectoryPathsUtf8_.push_back(std::move(importedRootUtf8));
            }
            if (requiresImageDirectory) {
                auto imagesCreated = ensureDestinationDirectory(imageRoot);
                if (!imagesCreated) {
                    return Core::failure(std::move(imagesCreated.error()));
                }
                if (*imagesCreated) {
                    ingress.createdDirectoryPathsUtf8_.push_back(std::move(imageRootUtf8));
                }
            }
            if (requiresAudioDirectory) {
                auto audioCreated = ensureDestinationDirectory(audioRoot);
                if (!audioCreated) {
                    return Core::failure(std::move(audioCreated.error()));
                }
                if (*audioCreated) {
                    ingress.createdDirectoryPathsUtf8_.push_back(std::move(audioRootUtf8));
                }
            }
        }

        for (auto& copy : copies) {
            if (const auto status = checkStopped(stopToken); !status) {
                return Core::failure(std::move(status.error()));
            }
            if (auto status = validateRegularPhysicalFile(
                    copy.source, "Editor import source changed before it could be copied");
                !status) {
                return Core::failure(std::move(status.error()));
            }
            auto destinationStatus = existingStatus(copy.destination);
            if (!destinationStatus) {
                return Core::failure(std::move(destinationStatus.error()));
            }
            if (destinationStatus->has_value()) {
                Core::Error error{
                    Core::CoreErrorCode::AlreadyExists,
                    "Editor import destination changed while the selection was being prepared"};
                error.addContext("destinationPath", copy.destinationUtf8);
                return Core::failure(std::move(error));
            }

            std::error_code copyError;
            const bool copied = std::filesystem::copy_file(
                copy.source, copy.destination, std::filesystem::copy_options::none, copyError);
            if (copyError || !copied) {
                return Core::failure(ioError("Editor could not copy an external import file",
                                             copy.destination, copyError));
            }
            ingress.createdFilePathsUtf8_.push_back(std::move(copy.destinationUtf8));
            ++ingress.copiedFileCount_;

            auto copiedContentMatches = filesHaveSameContent(
                copy.source, copy.destination, stopToken);
            if (!copiedContentMatches) {
                return Core::failure(std::move(copiedContentMatches.error()));
            }
            if (!*copiedContentMatches) {
                return Core::failure(
                    Core::CoreErrorCode::Io,
                    "Editor external import copy did not preserve the complete source file");
            }
        }

        for (const auto& verification : verifications) {
            if (const auto status = checkStopped(stopToken); !status) {
                return Core::failure(std::move(status.error()));
            }
            if (auto status = validateRegularPhysicalFile(
                    verification.destination,
                    "Editor import destination changed while the selection was being prepared");
                !status) {
                return Core::failure(std::move(status.error()));
            }
            auto sameContent = filesHaveSameContent(
                verification.source, verification.destination, stopToken);
            if (!sameContent) {
                return Core::failure(std::move(sameContent.error()));
            }
            if (!*sameContent) {
                return Core::failure(
                    Core::CoreErrorCode::Io,
                    "Editor import source changed while a reusable project copy was selected");
            }
        }

        return ingress;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor could not prepare the import file transaction");
    } catch (const std::filesystem::filesystem_error& exception) {
        Core::Error error{Core::CoreErrorCode::Io,
                          "Editor import file transaction failed"};
        error.setNativeCode(exception.code().value());
        return Core::failure(std::move(error));
    }
}

} // namespace Tina::EditorApp::Detail
