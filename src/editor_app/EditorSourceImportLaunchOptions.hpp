#pragma once

#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Tina::EditorApp::Detail {

inline constexpr Core::u32 EditorSourceImportPathByteCapacity =
    AssetFormat::SourceImportWire::MaxPathBytes;
inline constexpr Core::u32 EditorSourceImportUnitCapacity =
    AssetFormat::SourceImportWire::MaxUnits;

enum class EditorSourceImportLaunchUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
};

struct EditorSourceImportLaunchUnit final {
    EditorSourceImportLaunchUnitKind kind = EditorSourceImportLaunchUnitKind::CatalogRecipe;
    std::string pathUtf8{};

    friend bool operator==(const EditorSourceImportLaunchUnit&,
                           const EditorSourceImportLaunchUnit&) = default;
};

struct EditorSourceImportLaunchOptions final {
    std::string projectRootUtf8{};
    std::vector<EditorSourceImportLaunchUnit> intendedUnits{};
    bool importOnStart = false;
};

// Parses only source-import arguments. A successful false result leaves options untouched so the
// caller can continue through the Editor's existing launch-option parser.
[[nodiscard]] Core::Result<bool>
parseEditorSourceImportLaunchOption(std::string_view argument,
                                    EditorSourceImportLaunchOptions& options);

// Call after all launch arguments have been consumed. Import units are an explicit complete set;
// automatic import therefore requires both a project root and at least one unit.
[[nodiscard]] Core::Status
validateEditorSourceImportLaunchOptions(const EditorSourceImportLaunchOptions& options);

} // namespace Tina::EditorApp::Detail
