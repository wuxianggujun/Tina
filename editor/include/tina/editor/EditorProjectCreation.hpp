#pragma once

#include <tina/editor/EditorProjectWorkspace.hpp>

#include <string_view>

namespace Tina::Editor {

struct EditorProjectCreationRequest final {
    std::string_view projectRootUtf8{};
    std::string_view sourceDirectoryUtf8{"Source"};
    std::string_view cookedCatalogDirectoryUtf8{"Catalog"};
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    EditorProjectWorkspaceConfig workspaceConfig{};
};

// Creates one empty project directory transaction. An existing empty project
// root may be adopted; an existing non-empty root is never modified. Directory
// names are single relative UTF-8 path components. On success the returned
// workspace owns canonical path strings and no filesystem types escape this API.
[[nodiscard]] Core::Result<EditorProjectWorkspace>
CreateNewEditorProject(EditorProjectCreationRequest request);

} // namespace Tina::Editor
