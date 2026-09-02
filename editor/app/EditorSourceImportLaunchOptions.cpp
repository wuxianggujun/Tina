#include "EditorSourceImportLaunchOptions.hpp"

#include "core/io/PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <new>
#include <utility>

namespace Tina::EditorApp::Detail {
namespace {

using Core::Detail::pathIsSameOrDescendant;

// Option names, not prefixes: ArgScanner appends the '=' itself when it sees that spelling, and
// these double as the names in the duplicate and validation messages.
inline constexpr std::string_view ProjectRootOption = "--project-root";
inline constexpr std::string_view ImportRecipeOption = "--import-recipe";
inline constexpr std::string_view ImportGltfOption = "--import-gltf";
inline constexpr std::string_view ImportTextureOption = "--import-texture";
inline constexpr std::string_view ImportAudioOption = "--import-audio";
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
        return Core::Detail::pathFromUtf8Bytes(path);
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Could not retain source-import path for validation");
    } catch (const std::filesystem::filesystem_error&) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Source-import path is not valid on this platform");
    }
}

// Text overload: decodes through the validating utf8Path above, then normalizes, which the path
// overload in PathUtil.hpp deliberately leaves to its caller.
[[nodiscard]] bool pathsReferToSameLocation(std::string_view left,
                                            std::string_view right) noexcept
{
    try {
        auto leftPath = utf8Path(left);
        auto rightPath = utf8Path(right);
        if (!leftPath || !rightPath) {
            return false;
        }
        return Core::Detail::pathsReferToSameLocation(leftPath->lexically_normal(),
                                                      rightPath->lexically_normal());
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

    bool extensionMatches = false;
    switch (kind) {
    case EditorSourceImportLaunchUnitKind::CatalogRecipe:
        extensionMatches = extension == ".recipe";
        break;
    case EditorSourceImportLaunchUnitKind::Gltf:
        extensionMatches = extension == ".gltf" || extension == ".glb";
        break;
    case EditorSourceImportLaunchUnitKind::Texture:
        extensionMatches = extension == ".png" || extension == ".jpg" ||
                           extension == ".jpeg";
        break;
    case EditorSourceImportLaunchUnitKind::Audio:
        extensionMatches = extension == ".wav";
        break;
    }
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
parseEditorSourceImportLaunchOption(Core::ArgScanner& scanner,
                                    EditorSourceImportLaunchOptions& options)
{
    if (const auto path = scanner.value(ProjectRootOption)) {
        if (!options.projectRootUtf8.empty()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Duplicate --project-root argument");
        }
        if (auto status = validateProjectRoot(*path); !status) {
            return Core::failure(std::move(status.error()));
        }
        try {
            options.projectRootUtf8.assign(*path);
            return true;
        } catch (const std::bad_alloc&) {
            return Core::failure(Core::CoreErrorCode::OutOfMemory,
                                 "Could not retain --project-root");
        }
    }
    if (const auto path = scanner.value(ImportRecipeOption)) {
        return appendImportUnit(*path, ImportRecipeOption,
                                EditorSourceImportLaunchUnitKind::CatalogRecipe, options);
    }
    if (const auto path = scanner.value(ImportGltfOption)) {
        return appendImportUnit(*path, ImportGltfOption,
                                EditorSourceImportLaunchUnitKind::Gltf, options);
    }
    if (const auto path = scanner.value(ImportTextureOption)) {
        return appendImportUnit(*path, ImportTextureOption,
                                EditorSourceImportLaunchUnitKind::Texture, options);
    }
    if (const auto path = scanner.value(ImportAudioOption)) {
        return appendImportUnit(*path, ImportAudioOption,
                                EditorSourceImportLaunchUnitKind::Audio, options);
    }
    if (scanner.flag(ImportOnStartArgument)) {
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
        case EditorSourceImportLaunchUnitKind::Texture:
            optionName = "--import-texture";
            break;
        case EditorSourceImportLaunchUnitKind::Audio:
            optionName = "--import-audio";
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
