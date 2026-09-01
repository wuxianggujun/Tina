#pragma once

#include "EditorSourceImportLimits.hpp"

#include <tina/core/error/Result.hpp>
#include <tina/core/text/ArgParser.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Tina::EditorApp::Detail {

enum class EditorSourceImportLaunchUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
    Texture = 2,
    Audio = 3,
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

// Parses only source-import arguments from the scanner's current token. A successful false result
// leaves options untouched and consumes nothing, so the caller can continue through the Editor's
// existing launch-option parser.
//
// Takes the scanner rather than a lone token because --name value has to reach past the current
// token for its value, which a string_view cannot do. Callers must test scanner.failed() before
// treating a false result as an unknown argument: an option that appeared without its value also
// returns false.
[[nodiscard]] Core::Result<bool>
parseEditorSourceImportLaunchOption(Core::ArgScanner& scanner,
                                    EditorSourceImportLaunchOptions& options);

// Call after all launch arguments have been consumed. Import units are an explicit complete set;
// automatic import therefore requires both a project root and at least one unit.
[[nodiscard]] Core::Status
validateEditorSourceImportLaunchOptions(const EditorSourceImportLaunchOptions& options);

} // namespace Tina::EditorApp::Detail
