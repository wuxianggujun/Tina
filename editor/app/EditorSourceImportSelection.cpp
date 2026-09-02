#include "EditorSourceImportSelection.hpp"

#include "core/io/PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
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

using Core::Detail::pathFromUtf8Bytes;
using Core::Detail::pathIsSameOrDescendant;
using Core::Detail::pathsReferToSameLocation;
using Core::Detail::pathToUtf8;

// Normalizes both sides before comparing, which pathsReferToSameLocation deliberately does not do.
// lexically_normal can throw on allocation failure and this is a noexcept predicate, so a failure
// answers "not the same location" rather than propagating.
[[nodiscard]] bool sourcePathsReferToSameLocation(std::string_view left,
                                                  std::string_view right) noexcept
{
    try {
        const auto leftPath = pathFromUtf8Bytes(left).lexically_normal();
        const auto rightPath = pathFromUtf8Bytes(right).lexically_normal();
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
        unit.kind != EditorSourceImportUnitKind::Gltf &&
        unit.kind != EditorSourceImportUnitKind::Texture &&
        unit.kind != EditorSourceImportUnitKind::Audio) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import unit kind is invalid");
    }
    if (unit.mediaAssetId &&
        unit.kind != EditorSourceImportUnitKind::Texture &&
        unit.kind != EditorSourceImportUnitKind::Audio) {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "Editor source import media AssetId requires a Texture or Audio unit");
    }

    try {
        const auto sourceRoot = pathFromUtf8Bytes(sourceRootUtf8);
        const auto candidate = pathFromUtf8Bytes(unit.sourcePathUtf8);
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

        auto selectedKind = editorSourceImportUnitKindForPath(
            pathToUtf8(physicalCandidate));
        if (!selectedKind) {
            return Core::failure(std::move(selectedKind.error()));
        }
        if (*selectedKind != unit.kind) {
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
    auto kind = editorSourceImportUnitKindForPath(selectedPathUtf8);
    if (!kind) {
        return Core::failure(std::move(kind.error()));
    }
    try {
        return EditorSourceImportUnit{
            .kind = *kind,
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

Core::Result<EditorSourceImportUnitKind>
editorSourceImportUnitKindForPath(std::string_view sourcePathUtf8)
{
    try {
        const auto path = pathFromUtf8Bytes(sourcePathUtf8);
        std::string extension = pathToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        if (extension == ".recipe") {
            return EditorSourceImportUnitKind::CatalogRecipe;
        }
        if (extension == ".gltf" || extension == ".glb") {
            return EditorSourceImportUnitKind::Gltf;
        }
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
            return EditorSourceImportUnitKind::Texture;
        }
        if (extension == ".wav") {
            return EditorSourceImportUnitKind::Audio;
        }
        Core::Error error{
            Core::CoreErrorCode::InvalidArgument,
            "Editor source import supports only .recipe, .gltf, .glb, .png, "
            ".jpg, .jpeg, and .wav files"};
        error.addContext("sourcePath", sourcePathUtf8);
        return Core::failure(std::move(error));
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor source import extension inspection allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Core::Error error{Core::CoreErrorCode::Io,
                          "Editor source import extension could not be inspected"};
        error.setNativeCode(exception.code().value());
        return Core::failure(std::move(error));
    }
}

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

Core::Result<std::vector<EditorSourceImportUnit>>
removeEditorSourceImportUnit(
    std::span<const EditorSourceImportUnit> currentIntendedUnits,
    Core::usize logicalIndex)
{
    if (logicalIndex >= currentIntendedUnits.size()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import removal index is out of range");
    }

    try {
        std::vector<EditorSourceImportUnit> intendedUnits;
        intendedUnits.reserve(currentIntendedUnits.size() - 1U);
        intendedUnits.insert(intendedUnits.end(), currentIntendedUnits.begin(),
                             currentIntendedUnits.begin() +
                                 static_cast<std::ptrdiff_t>(logicalIndex));
        intendedUnits.insert(intendedUnits.end(),
                             currentIntendedUnits.begin() +
                                 static_cast<std::ptrdiff_t>(logicalIndex + 1U),
                             currentIntendedUnits.end());
        return intendedUnits;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor could not remove the source import unit");
    }
}

} // namespace Tina::EditorApp::Detail
