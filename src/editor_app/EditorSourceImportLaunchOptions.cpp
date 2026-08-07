#include "EditorSourceImportLaunchOptions.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <filesystem>
#include <new>
#include <utility>

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
                           return unit.kind == kind && unit.pathUtf8 == path;
                       });
}

[[nodiscard]] Core::Result<bool>
appendImportUnit(std::string_view path, std::string_view optionName,
                 EditorSourceImportLaunchUnitKind kind,
                 EditorSourceImportLaunchOptions& options)
{
    if (auto status = validatePathText(path, optionName); !status) {
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
        if (auto status = validatePathText(unit.pathUtf8, optionName); !status) {
            return status;
        }
        const auto duplicate = std::find_if(
            options.intendedUnits.begin(), options.intendedUnits.begin() + index,
            [&unit](const EditorSourceImportLaunchUnit& candidate) {
                return candidate.kind == unit.kind && candidate.pathUtf8 == unit.pathUtf8;
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
    if (options.importOnStart && options.intendedUnits.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "--import-on-start requires at least one import unit");
    }
    return Core::success();
}

} // namespace Tina::EditorApp::Detail
