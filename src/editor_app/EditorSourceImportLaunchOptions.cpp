#include "EditorSourceImportLaunchOptions.hpp"

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

inline constexpr std::string_view ProjectRootPrefix = "--project-root=";
inline constexpr std::string_view ImportRecipePrefix = "--import-recipe=";
inline constexpr std::string_view ImportGltfPrefix = "--import-gltf=";
inline constexpr std::string_view ImportOnStartArgument = "--import-on-start";

[[nodiscard]] Core::Status validatePathText(std::string_view path, std::string_view optionName)
{
    if (path.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             std::string{optionName} + " must not be empty");
    }
    if (path.size() > EditorSourceImportPathByteCapacity) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             std::string{optionName} + " exceeds the source-import path capacity");
    }
    if (!Core::isStrictUtf8WithoutNul(path)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             std::string{optionName} + " must be strict UTF-8 without NUL");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<std::filesystem::path> utf8Path(std::string_view path)
{
    try {
        std::u8string encoded;
        encoded.reserve(path.size());
        for (const char byte : path) {
            encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        return std::filesystem::path{std::move(encoded)};
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Could not retain source-import path for validation");
    } catch (const std::filesystem::filesystem_error&) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Source-import path is not valid on this platform");
    }
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

[[nodiscard]] bool pathsReferToSameLocation(std::string_view left,
                                            std::string_view right) noexcept
{
    try {
        auto leftPath = utf8Path(left);
        auto rightPath = utf8Path(right);
        if (!leftPath || !rightPath) {
            return false;
        }
        const auto normalizedLeft = leftPath->lexically_normal();
        const auto normalizedRight = rightPath->lexically_normal();
        return pathIsSameOrDescendant(normalizedLeft, normalizedRight) &&
               pathIsSameOrDescendant(normalizedRight, normalizedLeft);
    } catch (...) {
        return false;
    }
}

[[nodiscard]] Core::Status validateImportUnitPath(
    std::string_view path, std::string_view optionName,
    EditorSourceImportLaunchUnitKind kind)
{
    if (auto status = validatePathText(path, optionName); !status) {
        return status;
    }
    auto nativePath = utf8Path(path);
    if (!nativePath) {
        return Core::failure(std::move(nativePath.error()));
    }
    if (!nativePath->is_absolute()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             std::string{optionName} + " must be an absolute path");
    }

    std::string extension;
    try {
        const auto encoded = nativePath->extension().u8string();
        extension.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Could not validate source-import extension");
    } catch (const std::filesystem::filesystem_error&) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Source-import extension is invalid on this platform");
    }

    const bool extensionMatches =
        kind == EditorSourceImportLaunchUnitKind::CatalogRecipe
            ? extension == ".recipe"
            : extension == ".gltf" || extension == ".glb";
    if (!extensionMatches) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             std::string{optionName} + " has an incompatible file extension");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateProjectRoot(std::string_view path)
{
    if (auto status = validatePathText(path, "--project-root"); !status) {
        return status;
    }
    auto nativePath = utf8Path(path);
    if (!nativePath) {
        return Core::failure(std::move(nativePath.error()));
    }
    if (!nativePath->is_absolute()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "--project-root must be an absolute path");
    }
    return Core::success();
}

[[nodiscard]] bool containsUnit(const EditorSourceImportLaunchOptions& options,
                                EditorSourceImportLaunchUnitKind kind,
                                std::string_view path) noexcept
{
    return std::any_of(options.intendedUnits.begin(), options.intendedUnits.end(),
                       [kind, path](const EditorSourceImportLaunchUnit& unit) {
                           return unit.kind == kind &&
                                  pathsReferToSameLocation(unit.pathUtf8, path);
                       });
}

[[nodiscard]] Core::Result<bool>
appendImportUnit(std::string_view path, std::string_view optionName,
                 EditorSourceImportLaunchUnitKind kind,
                 EditorSourceImportLaunchOptions& options)
{
    if (auto status = validateImportUnitPath(path, optionName, kind); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (containsUnit(options, kind, path)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             std::string{"Duplicate "} + std::string{optionName} + " path");
    }
    if (options.intendedUnits.size() >= EditorSourceImportUnitCapacity) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Source-import launch unit capacity exceeded");
    }

    try {
        EditorSourceImportLaunchUnit unit{.kind = kind, .pathUtf8 = std::string{path}};
        options.intendedUnits.push_back(std::move(unit));
        return true;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Could not retain source-import launch unit");
    }
}

} // namespace

Core::Result<bool>
parseEditorSourceImportLaunchOption(std::string_view argument,
                                    EditorSourceImportLaunchOptions& options)
{
    if (argument.starts_with(ProjectRootPrefix)) {
        if (!options.projectRootUtf8.empty()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Duplicate --project-root argument");
        }
        const std::string_view path = argument.substr(ProjectRootPrefix.size());
        if (auto status = validateProjectRoot(path); !status) {
            return Core::failure(std::move(status.error()));
        }
        try {
            options.projectRootUtf8.assign(path);
            return true;
        } catch (const std::bad_alloc&) {
            return Core::failure(Core::CoreErrorCode::OutOfMemory,
                                 "Could not retain --project-root");
        }
    }
    if (argument.starts_with(ImportRecipePrefix)) {
        return appendImportUnit(argument.substr(ImportRecipePrefix.size()), "--import-recipe",
                                EditorSourceImportLaunchUnitKind::CatalogRecipe, options);
    }
    if (argument.starts_with(ImportGltfPrefix)) {
        return appendImportUnit(argument.substr(ImportGltfPrefix.size()), "--import-gltf",
                                EditorSourceImportLaunchUnitKind::Gltf, options);
    }
    if (argument == ImportOnStartArgument) {
        if (options.importOnStart) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Duplicate --import-on-start argument");
        }
        options.importOnStart = true;
        return true;
    }
    return false;
}

Core::Status
validateEditorSourceImportLaunchOptions(const EditorSourceImportLaunchOptions& options)
{
    if (!options.projectRootUtf8.empty()) {
        if (auto status = validateProjectRoot(options.projectRootUtf8); !status) {
            return status;
        }
    }
    if (options.intendedUnits.size() > EditorSourceImportUnitCapacity) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Source-import launch unit capacity exceeded");
    }
    for (Core::usize index = 0; index < options.intendedUnits.size(); ++index) {
        const auto& unit = options.intendedUnits[index];
        std::string_view optionName{};
        switch (unit.kind) {
        case EditorSourceImportLaunchUnitKind::CatalogRecipe:
            optionName = "--import-recipe";
            break;
        case EditorSourceImportLaunchUnitKind::Gltf:
            optionName = "--import-gltf";
            break;
        default:
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Source-import launch unit kind is invalid");
        }
        if (auto status = validateImportUnitPath(unit.pathUtf8, optionName, unit.kind); !status) {
            return status;
        }
        const auto duplicate = std::find_if(
            options.intendedUnits.begin(), options.intendedUnits.begin() + index,
            [&unit](const EditorSourceImportLaunchUnit& candidate) {
                return candidate.kind == unit.kind &&
                       pathsReferToSameLocation(candidate.pathUtf8, unit.pathUtf8);
            });
        if (duplicate != options.intendedUnits.begin() + index) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 std::string{"Duplicate "} + std::string{optionName} + " path");
        }
    }
    if (!options.intendedUnits.empty() && options.projectRootUtf8.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Source-import units require --project-root");
    }
    if (!options.intendedUnits.empty()) {
        auto projectRoot = utf8Path(options.projectRootUtf8);
        if (!projectRoot) {
            return Core::failure(std::move(projectRoot.error()));
        }
        const auto sourceRoot = (*projectRoot / "Source").lexically_normal();
        for (const auto& unit : options.intendedUnits) {
            auto unitPath = utf8Path(unit.pathUtf8);
            if (!unitPath) {
                return Core::failure(std::move(unitPath.error()));
            }
            const auto normalizedUnit = unitPath->lexically_normal();
            if (pathIsSameOrDescendant(sourceRoot, normalizedUnit) ||
                !pathIsSameOrDescendant(normalizedUnit, sourceRoot)) {
                return Core::failure(
                    Core::CoreErrorCode::PermissionDenied,
                    "Source-import unit must be a file below the project Source directory");
            }
        }
    }
    if (options.importOnStart && options.intendedUnits.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "--import-on-start requires at least one import unit");
    }
    return Core::success();
}

} // namespace Tina::EditorApp::Detail
