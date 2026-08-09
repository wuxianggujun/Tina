#pragma once

#include "EditorSourceImportLimits.hpp"
#include "EditorSourceImportService.hpp"

#include <tina/core/error/Result.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::EditorApp::Detail {

struct EditorSourceImportSelectionResult final {
    std::vector<EditorSourceImportUnit> intendedUnits{};
    Core::u32 selectedPathCount = 0;
    Core::u32 addedUnitCount = 0;
};

[[nodiscard]] Core::Result<std::vector<EditorSourceImportUnit>>
validateEditorSourceImportIntendedSet(
    std::string_view sourceRootUtf8,
    std::span<const EditorSourceImportUnit> intendedUnits,
    Core::u32 maxUnits = EditorSourceImportUnitCapacity);

// Builds a normalized candidate without mutating the current intended set. Existing or repeated
// selections trigger a reimport but are retained only once in the returned complete set.
[[nodiscard]] Core::Result<EditorSourceImportSelectionResult>
mergeEditorSourceImportSelection(
    std::string_view sourceRootUtf8,
    std::span<const EditorSourceImportUnit> currentIntendedUnits,
    std::span<const std::string> selectedPathsUtf8,
    Core::u32 maxUnits = EditorSourceImportUnitCapacity);

} // namespace Tina::EditorApp::Detail
