#include "EditorSourceImportSelection.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <new>
#include <utility>

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

[[nodiscard]] bool sourcePathsReferToSameLocation(std::string_view left,
                                                  std::string_view right) noexcept
{
    try {
        const auto leftPath = std::filesystem::u8path(left.begin(), left.end()).lexically_normal();
        const auto rightPath = std::filesystem::u8path(right.begin(), right.end()).lexically_normal();
        return pathsReferToSameLocation(leftPath, rightPath);
    } catch (...) {
        return false;
    }
}

[[nodiscard]] Core::Result<EditorSourceImportUnit>
validateUnit(std::string_view sourceRootUtf8, const EditorSourceImportUnit& unit)
{
    if (unit.sourcePathUtf8.empty() ||
        unit.sourcePathUtf8.size() > AssetFormat::SourceImportWire::MaxPathBytes ||
        !Core::isStrictUtf8WithoutNul(unit.sourcePathUtf8)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import path must be bounded strict UTF-8 without NUL");
    }
    if (unit.kind != EditorSourceImportUnitKind::CatalogRecipe &&
        unit.kind != EditorSourceImportUnitKind::Gltf) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import unit kind is invalid");
    }

    try {
        const auto sourceRoot =
            std::filesystem::u8path(sourceRootUtf8.begin(), sourceRootUtf8.end());
        const auto candidate = std::filesystem::u8path(
            unit.sourcePathUtf8.begin(), unit.sourcePathUtf8.end());
        if (!candidate.is_absolute()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor source import path must be absolute");
        }

        std::error_code statusError;
        const auto candidateStatus = std::filesystem::symlink_status(candidate, statusError);
        if (statusError || !std::filesystem::is_regular_file(candidateStatus)) {
            Core::Error error{
                statusError ? Core::CoreErrorCode::Io
                            : Core::CoreErrorCode::InvalidArgument,
                "Editor source import path must name an existing physical file"};
            if (statusError) {
                error.setNativeCode(statusError.value());
            }
            error.addContext("sourcePath", unit.sourcePathUtf8);
            return Core::failure(std::move(error));
        }
#if defined(_WIN32)
        const DWORD attributes = ::GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            Core::Error error{Core::CoreErrorCode::Io,
                              "Editor could not inspect source import file attributes"};
            error.setNativeCode(static_cast<Core::i64>(::GetLastError()));
            error.addContext("sourcePath", unit.sourcePathUtf8);
            return Core::failure(std::move(error));
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return Core::failure(Core::CoreErrorCode::PermissionDenied,
                                 "Editor source import files must not be reparse points");
        }
#endif

        std::error_code canonicalError;
        const auto physicalSourceRoot =
            std::filesystem::weakly_canonical(sourceRoot, canonicalError);
        if (canonicalError) {
            Core::Error error{Core::CoreErrorCode::Io,
                              "Editor could not resolve the project Source directory"};
            error.setNativeCode(canonicalError.value());
            return Core::failure(std::move(error));
        }
        const auto physicalCandidate =
            std::filesystem::weakly_canonical(candidate, canonicalError);
        if (canonicalError) {
            Core::Error error{Core::CoreErrorCode::Io,
                              "Editor could not resolve the source import file"};
            error.setNativeCode(canonicalError.value());
            return Core::failure(std::move(error));
        }
        if (pathsReferToSameLocation(physicalCandidate, physicalSourceRoot) ||
            !pathIsSameOrDescendant(physicalCandidate, physicalSourceRoot)) {
            return Core::failure(
                Core::CoreErrorCode::PermissionDenied,
                "Editor source import file must be below the project's Source directory");
        }

        std::string extension = pathToUtf8(physicalCandidate.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        const bool extensionMatches =
            unit.kind == EditorSourceImportUnitKind::CatalogRecipe
                ? extension == ".recipe"
                : extension == ".gltf" || extension == ".glb";
        if (!extensionMatches) {
            return Core::failure(
                Core::CoreErrorCode::InvalidArgument,
                "Editor source import kind does not match the selected file extension");
        }

        auto validated = unit;
        validated.sourcePathUtf8 = pathToUtf8(physicalCandidate.lexically_normal());
        return validated;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor source import path validation allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Core::Error error{Core::CoreErrorCode::Io,
                          "Editor source import path validation failed"};
        error.setNativeCode(exception.code().value());
        return Core::failure(std::move(error));
    }
}

[[nodiscard]] Core::Result<EditorSourceImportUnit>
unitFromSelectedPath(std::string_view selectedPathUtf8)
{
    try {
        const auto path = std::filesystem::u8path(
            selectedPathUtf8.begin(), selectedPathUtf8.end());
        std::string extension = pathToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });

        EditorSourceImportUnitKind kind{};
        if (extension == ".recipe") {
            kind = EditorSourceImportUnitKind::CatalogRecipe;
        } else if (extension == ".gltf" || extension == ".glb") {
            kind = EditorSourceImportUnitKind::Gltf;
        } else {
            Core::Error error{
                Core::CoreErrorCode::InvalidArgument,
                "Editor source import supports only .recipe, .gltf, and .glb files"};
            error.addContext("sourcePath", selectedPathUtf8);
            return Core::failure(std::move(error));
        }
        return EditorSourceImportUnit{
            .kind = kind,
            .sourcePathUtf8 = std::string{selectedPathUtf8},
        };
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor source import selection allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Core::Error error{Core::CoreErrorCode::Io,
                          "Editor source import selection path could not be inspected"};
        error.setNativeCode(exception.code().value());
        return Core::failure(std::move(error));
    }
}

} // namespace

Core::Result<std::vector<EditorSourceImportUnit>>
validateEditorSourceImportIntendedSet(
    std::string_view sourceRootUtf8,
    std::span<const EditorSourceImportUnit> intendedUnits,
    Core::u32 maxUnits)
{
    if (maxUnits == 0U || maxUnits > EditorSourceImportUnitCapacity) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import unit capacity is invalid");
    }
    if (intendedUnits.size() > maxUnits) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Editor source import intended unit capacity exceeded");
    }

    try {
        std::vector<EditorSourceImportUnit> validatedUnits;
        validatedUnits.reserve(intendedUnits.size());
        for (const auto& unit : intendedUnits) {
            auto validated = validateUnit(sourceRootUtf8, unit);
            if (!validated) {
                return Core::failure(std::move(validated.error()));
            }
            const bool duplicate = std::any_of(
                validatedUnits.begin(), validatedUnits.end(),
                [&](const auto& existing) {
                    return existing.kind == validated->kind &&
                           sourcePathsReferToSameLocation(existing.sourcePathUtf8,
                                                          validated->sourcePathUtf8);
                });
            if (duplicate) {
                return Core::failure(
                    Core::CoreErrorCode::InvalidArgument,
                    "Editor source import intended set contains the same physical file twice");
            }
            validatedUnits.push_back(std::move(*validated));
        }
        return validatedUnits;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor could not validate the source import intended set");
    }
}

Core::Result<EditorSourceImportSelectionResult>
mergeEditorSourceImportSelection(
    std::string_view sourceRootUtf8,
    std::span<const EditorSourceImportUnit> currentIntendedUnits,
    std::span<const std::string> selectedPathsUtf8,
    Core::u32 maxUnits)
{
    if (maxUnits == 0U || maxUnits > EditorSourceImportUnitCapacity) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import unit capacity is invalid");
    }
    if (selectedPathsUtf8.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import selection must not be empty");
    }
    if (selectedPathsUtf8.size() > maxUnits) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Editor source import selection capacity exceeded");
    }

    auto intendedUnits = validateEditorSourceImportIntendedSet(
        sourceRootUtf8, currentIntendedUnits, maxUnits);
    if (!intendedUnits) {
        return Core::failure(std::move(intendedUnits.error()));
    }

    try {
        Core::u32 addedUnitCount = 0;
        for (const auto& selectedPathUtf8 : selectedPathsUtf8) {
            auto candidate = unitFromSelectedPath(selectedPathUtf8);
            if (!candidate) {
                return Core::failure(std::move(candidate.error()));
            }
            auto validated = validateUnit(sourceRootUtf8, *candidate);
            if (!validated) {
                return Core::failure(std::move(validated.error()));
            }
            const bool alreadyIntended = std::any_of(
                intendedUnits->begin(), intendedUnits->end(),
                [&](const auto& existing) {
                    return existing.kind == validated->kind &&
                           sourcePathsReferToSameLocation(existing.sourcePathUtf8,
                                                          validated->sourcePathUtf8);
                });
            if (alreadyIntended) {
                continue;
            }
            if (intendedUnits->size() >= maxUnits) {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "Editor source import intended unit capacity exceeded");
            }
            intendedUnits->push_back(std::move(*validated));
            ++addedUnitCount;
        }
        return EditorSourceImportSelectionResult{
            .intendedUnits = std::move(*intendedUnits),
            .selectedPathCount = static_cast<Core::u32>(selectedPathsUtf8.size()),
            .addedUnitCount = addedUnitCount,
        };
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor could not retain the source import selection");
    }
}

} // namespace Tina::EditorApp::Detail
