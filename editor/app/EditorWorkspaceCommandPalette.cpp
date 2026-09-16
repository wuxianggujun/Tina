#include "EditorWorkspaceState.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

struct CommandPaletteCatalogEntry final {
    EditorCommand command = EditorCommand::ShowAbout;
    std::string_view name{};
    std::string_view shortcut{};
};

inline constexpr std::array kCommandPaletteCatalog{
    CommandPaletteCatalogEntry{EditorCommand::Save, "Save", "Ctrl+S"},
    CommandPaletteCatalogEntry{EditorCommand::SaveAs, "Save As", "Ctrl+Shift+S"},
    CommandPaletteCatalogEntry{EditorCommand::CloseActiveDocument, "Close Document", {}},
    CommandPaletteCatalogEntry{EditorCommand::CreateProject, "New Project", {}},
    CommandPaletteCatalogEntry{EditorCommand::OpenProject, "Open Project", {}},
    CommandPaletteCatalogEntry{EditorCommand::ImportSource, "Import Files", {}},
    CommandPaletteCatalogEntry{EditorCommand::ImportStreamAudio, "Import Stream Audio", {}},
    CommandPaletteCatalogEntry{EditorCommand::Undo, "Undo", "Ctrl+Z"},
    CommandPaletteCatalogEntry{EditorCommand::Redo, "Redo", "Ctrl+Y"},
    CommandPaletteCatalogEntry{EditorCommand::SceneCopy, "Copy", "Ctrl+C"},
    CommandPaletteCatalogEntry{EditorCommand::ScenePaste, "Paste", "Ctrl+V"},
    CommandPaletteCatalogEntry{EditorCommand::SceneDuplicate, "Duplicate", "Ctrl+D"},
    CommandPaletteCatalogEntry{EditorCommand::SceneDelete, "Delete", "Delete"},
    CommandPaletteCatalogEntry{EditorCommand::SceneAdd, "Add Node", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::SceneSaveSubtreeTemplate, "Save as Prefab2D", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::ScenePasteSubtreeTemplate, "Place Prefab2D Instance", {}},
    CommandPaletteCatalogEntry{EditorCommand::SceneReparentRoot, "Move to Root", {}},
    CommandPaletteCatalogEntry{EditorCommand::SwitchToWorld2D, "Switch to 2D", "Ctrl+1"},
    CommandPaletteCatalogEntry{EditorCommand::SwitchToWorld3D, "Switch to 3D", "Ctrl+2"},
    CommandPaletteCatalogEntry{EditorCommand::ViewportResetView, "Frame All", "Ctrl+0"},
    CommandPaletteCatalogEntry{EditorCommand::SceneFocus, "Focus Selection", "Ctrl+F"},
    CommandPaletteCatalogEntry{EditorCommand::ToggleCameraPreview, "Camera Preview", {}},
    CommandPaletteCatalogEntry{EditorCommand::ViewportCyclePreset, "Cycle 3D View", {}},
    CommandPaletteCatalogEntry{EditorCommand::ViewportPresetTop, "3D View Top", {}},
    CommandPaletteCatalogEntry{EditorCommand::ViewportPresetFront, "3D View Front", {}},
    CommandPaletteCatalogEntry{EditorCommand::ViewportPresetRight, "3D View Right", {}},
    CommandPaletteCatalogEntry{EditorCommand::PlayStartOrResume, "Play / Resume", "F6"},
    CommandPaletteCatalogEntry{EditorCommand::PlayPause, "Pause", {}},
    CommandPaletteCatalogEntry{EditorCommand::PlayStep, "Step", "F7"},
    CommandPaletteCatalogEntry{EditorCommand::PlayStop, "Stop", "F8"},
    CommandPaletteCatalogEntry{EditorCommand::PaintTile, "Paint Tile", {}},
    CommandPaletteCatalogEntry{EditorCommand::EraseTile, "Erase Tile", {}},
    CommandPaletteCatalogEntry{EditorCommand::AddTileLayer, "Add Tile Layer", {}},
    CommandPaletteCatalogEntry{EditorCommand::AddObjectLayer, "Add Object Layer", {}},
    CommandPaletteCatalogEntry{EditorCommand::CookTileMapPreview, "Cook TileMap Preview", {}},
    CommandPaletteCatalogEntry{EditorCommand::BakeNavigation2D, "Bake Navigation 2D", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::GenerateTileMapGameplay, "Generate TileMap Gameplay", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationTogglePlayback, "Animation Play/Pause", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationPreviousFrame, "Animation Previous Frame", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationNextFrame, "Animation Next Frame", {}},
    CommandPaletteCatalogEntry{EditorCommand::AnimationAddFrame, "Animation Add Frame", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationDuplicateFrame, "Animation Duplicate Frame", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationDeleteFrame, "Animation Delete Frame", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationCycleMode, "Animation Cycle Mode", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::AnimationCookPreview, "Animation Cook Preview", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::OpenSelectedProjectAsset, "Open Selected Asset", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::InspectSelectedProjectAsset, "Inspect Selected Asset", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::ReimportSelectedProjectAsset, "Reimport Selected Asset", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::LocateProjectAssetSource, "Locate Source", {}},
    CommandPaletteCatalogEntry{EditorCommand::CopyProjectAssetId, "Copy Asset Id", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::CopyProjectAssetSourcePath, "Copy Source Path", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::RenameSelectedProjectAsset, "Rename Selected Asset", {}},
    CommandPaletteCatalogEntry{EditorCommand::NewProjectAssetFolder, "New Folder", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::RemoveSelectedProjectAsset, "Remove Selected Asset", {}},
    CommandPaletteCatalogEntry{
        EditorCommand::RefreshProjectCatalog, "Refresh Catalog", {}},
    CommandPaletteCatalogEntry{EditorCommand::FxPreviewPlay, "Fx Preview Play", {}},
    CommandPaletteCatalogEntry{EditorCommand::FxPreviewRestart, "Fx Preview Restart", {}},
    CommandPaletteCatalogEntry{EditorCommand::AudioPreviewPlay, "Audio Preview Play", {}},
    CommandPaletteCatalogEntry{EditorCommand::AudioPreviewStop, "Audio Preview Stop", {}},
    CommandPaletteCatalogEntry{EditorCommand::ShowAbout, "About Tina Editor", {}},
};

[[nodiscard]] constexpr char asciiLower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

[[nodiscard]] bool asciiContainsInsensitive(std::string_view text,
                                            std::string_view needle) noexcept
{
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > text.size()) {
        return false;
    }
    for (Tina::Core::usize offset = 0;
         offset + needle.size() <= text.size(); ++offset) {
        bool matches = true;
        for (Tina::Core::usize index = 0; index < needle.size(); ++index) {
            if (asciiLower(text[offset + index]) != asciiLower(needle[index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool asciiSubsequenceInsensitive(std::string_view text,
                                               std::string_view needle) noexcept
{
    if (needle.empty()) {
        return true;
    }
    Tina::Core::usize cursor = 0;
    for (char expected : needle) {
        expected = asciiLower(expected);
        bool found = false;
        while (cursor < text.size()) {
            if (asciiLower(text[cursor++]) == expected) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] u32 commandPaletteMatchRank(std::string_view name,
                                          std::string_view shortcut,
                                          std::string_view filter) noexcept
{
    if (filter.empty()) {
        return 1U;
    }
    if (asciiContainsInsensitive(name, filter)) {
        return 2U;
    }
    if (!shortcut.empty() && asciiContainsInsensitive(shortcut, filter)) {
        return 3U;
    }
    if (asciiSubsequenceInsensitive(name, filter)) {
        return 4U;
    }
    return 0U;
}

[[nodiscard]] bool commandAllowedDuringPlay(EditorCommand command) noexcept
{
    switch (command) {
    case EditorCommand::PlayStartOrResume:
    case EditorCommand::PlayPause:
    case EditorCommand::PlayStep:
    case EditorCommand::PlayStop:
    case EditorCommand::SceneFocus:
    case EditorCommand::ViewportResetView:
    case EditorCommand::ViewportCyclePreset:
    case EditorCommand::ViewportPresetTop:
    case EditorCommand::ViewportPresetFront:
    case EditorCommand::ViewportPresetRight:
    case EditorCommand::ShowAbout:
    case EditorCommand::ShowCommandPalette:
    case EditorCommand::HideCommandPalette:
    case EditorCommand::CommandPaletteExecute:
        return true;
    default:
        return false;
    }
}

} // namespace

auto EditorWorkspaceState::commandPaletteAvailability(EditorCommand command) const noexcept
    -> EditorWorkspaceState::CommandPaletteAvailability
{
    if (playSessionActive() && !commandAllowedDuringPlay(command)) {
        return {.enabled = false, .reason = "Play session is active"};
    }
    const bool hasSelection =
        stableEntityIdForHierarchyItem(selectionKey_) != 0U;
    const bool hasProjectSelection = observedProjectAssetSelectionIndex_.has_value();
    switch (command) {
    case EditorCommand::Undo:
        return activeCanUndo()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Nothing to undo"};
    case EditorCommand::Redo:
        return activeCanRedo()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Nothing to redo"};
    case EditorCommand::Save:
    case EditorCommand::SaveAs:
    case EditorCommand::CloseActiveDocument:
        return {.enabled = true};
    case EditorCommand::SceneCopy:
    case EditorCommand::SceneDuplicate:
    case EditorCommand::SceneDelete:
    case EditorCommand::SceneFocus:
    case EditorCommand::SceneSaveSubtreeTemplate:
    case EditorCommand::SceneReparentRoot:
        if (!sceneDocumentActive()) {
            return {.enabled = false,
                    .reason = "Requires a World2D or World3D document"};
        }
        if (!hasSelection) {
            return {.enabled = false, .reason = "Select a scene node"};
        }
        return {.enabled = true};
    case EditorCommand::ScenePaste:
    case EditorCommand::ScenePasteSubtreeTemplate:
        if (!sceneDocumentActive()) {
            return {.enabled = false,
                    .reason = "Requires a World2D or World3D document"};
        }
        if (command == EditorCommand::ScenePaste &&
            sceneClipboardKind_ == SceneClipboardKind::Empty) {
            return {.enabled = false, .reason = "Scene clipboard is empty"};
        }
        return {.enabled = true};
    case EditorCommand::SceneAdd:
        return sceneDocumentActive()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false,
                         .reason = "Requires a World2D or World3D document"};
    case EditorCommand::ToggleCameraPreview:
        return workspaceMode_ == WorkspaceMode::World2D && authoringEnabled()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Requires the 2D workspace"};
    case EditorCommand::ViewportCyclePreset:
    case EditorCommand::ViewportPresetTop:
    case EditorCommand::ViewportPresetFront:
    case EditorCommand::ViewportPresetRight:
        return workspaceMode_ == WorkspaceMode::World3D
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Requires the 3D workspace"};
    case EditorCommand::PlayPause:
    case EditorCommand::PlayStep:
    case EditorCommand::PlayStop:
        return playSessionActive()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Play session is not active"};
    case EditorCommand::PaintTile:
    case EditorCommand::EraseTile:
    case EditorCommand::AddTileLayer:
    case EditorCommand::AddObjectLayer:
    case EditorCommand::CookTileMapPreview:
    case EditorCommand::BakeNavigation2D:
    case EditorCommand::GenerateTileMapGameplay:
        return tileMapEditingContext()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Requires TileMap editing"};
    case EditorCommand::AnimationTogglePlayback:
    case EditorCommand::AnimationPreviousFrame:
    case EditorCommand::AnimationNextFrame:
    case EditorCommand::AnimationAddFrame:
    case EditorCommand::AnimationDuplicateFrame:
    case EditorCommand::AnimationDeleteFrame:
    case EditorCommand::AnimationCycleMode:
    case EditorCommand::AnimationCookPreview:
        return workspaceMode_ == WorkspaceMode::World2D
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false,
                         .reason = "Sprite animation is available in 2D"};
    case EditorCommand::FxPreviewPlay:
    case EditorCommand::FxPreviewRestart:
        return fxEditingContext()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Requires an open Fx2D document"};
    case EditorCommand::AudioPreviewPlay:
        return resolveAudioPreviewTarget().has_value() && authoringEnabled() &&
                       !playSessionActive()
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false,
                         .reason = "Select an AudioClip or AudioPlayer2D"};
    case EditorCommand::AudioPreviewStop:
        return audioPreviewPlaying_
                   ? CommandPaletteAvailability{.enabled = true}
                   : CommandPaletteAvailability{
                         .enabled = false, .reason = "Audio preview is not playing"};
    case EditorCommand::OpenSelectedProjectAsset:
    case EditorCommand::InspectSelectedProjectAsset:
    case EditorCommand::ReimportSelectedProjectAsset:
    case EditorCommand::CopyProjectAssetId:
    case EditorCommand::CopyProjectAssetSourcePath:
    case EditorCommand::RenameSelectedProjectAsset:
    case EditorCommand::RemoveSelectedProjectAsset:
    case EditorCommand::LocateProjectAssetSource:
        if (!hasProjectSelection) {
            return {.enabled = false, .reason = "Select a Project Asset"};
        }
        if (command == EditorCommand::LocateProjectAssetSource &&
            shellReveal_ == nullptr) {
            return {.enabled = false,
                    .reason = "This platform has no file-manager reveal"};
        }
        return {.enabled = true};
    case EditorCommand::NewProjectAssetFolder:
    case EditorCommand::RefreshProjectCatalog:
    case EditorCommand::CreateProject:
    case EditorCommand::OpenProject:
    case EditorCommand::ImportSource:
    case EditorCommand::ImportStreamAudio:
    case EditorCommand::SwitchToWorld2D:
    case EditorCommand::SwitchToWorld3D:
    case EditorCommand::ViewportResetView:
    case EditorCommand::PlayStartOrResume:
    case EditorCommand::ShowAbout:
        return {.enabled = true};
    default:
        return {.enabled = false, .reason = "Command is not available"};
    }
}

auto EditorWorkspaceState::showCommandPalette(Tina::PrimaryWindowUITreeUpdater& tree)
    -> Tina::Core::Status
{
    if (commandPaletteOpen_ || pendingSceneAddRequest_.has_value() ||
        spriteAssetPickerVisible_ || pendingSceneDeleteConfirmation_.has_value() ||
        pendingProjectAssetRemoveConfirmation_.has_value() ||
        pendingDirtyCloseKey_.has_value() ||
        pendingAutosaveRestoreKey_.has_value() || hierarchyRenameVisible_) {
        return Tina::Core::success();
    }
    commandPaletteFilterUtf8_.clear();
    if (auto status = refreshCommandPaletteUi(tree); !status) {
        return status;
    }
    if (auto status = tree.openDialog(commandPaletteDialog_.modal); !status) {
        return status;
    }
    commandPaletteOpen_ = true;
    pendingCommandPaletteFocus_ = true;
    pendingCommandPaletteFocusRestore_ = false;
    authoringFeedback_ = "Command palette: type to filter, Enter to run";
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideCommandPalette(Tina::PrimaryWindowUITreeUpdater& tree)
    -> Tina::Core::Status
{
    if (!commandPaletteOpen_) {
        return Tina::Core::success();
    }
    if (auto status = tree.dismissDialog(commandPaletteDialog_.modal); !status) {
        return status;
    }
    commandPaletteOpen_ = false;
    pendingCommandPaletteFocus_ = false;
    pendingCommandPaletteFocusRestore_ = true;
    commandPaletteFilterUtf8_.clear();
    commandPaletteRows_.clear();
    observedCommandPaletteSelectionIndex_.reset();
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshCommandPaletteUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    struct RankedMatch final {
        u32 rank = 0;
        Tina::Core::usize catalogIndex = 0;
    };
    std::array<RankedMatch, kCommandPaletteCatalog.size()> ranked{};
    Tina::Core::usize matchCount = 0;
    for (Tina::Core::usize index = 0; index < kCommandPaletteCatalog.size();
         ++index) {
        const auto& entry = kCommandPaletteCatalog[index];
        const u32 rank = commandPaletteMatchRank(
            entry.name, entry.shortcut, commandPaletteFilterUtf8_);
        if (rank == 0U) {
            continue;
        }
        ranked[matchCount++] = RankedMatch{.rank = rank, .catalogIndex = index};
    }
    std::stable_sort(
        ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(matchCount),
        [](const RankedMatch& left, const RankedMatch& right) {
            if (left.rank != right.rank) {
                return left.rank < right.rank;
            }
            return left.catalogIndex < right.catalogIndex;
        });
    try {
        commandPaletteRows_.clear();
        commandPaletteRows_.reserve(matchCount);
        for (Tina::Core::usize index = 0; index < matchCount; ++index) {
            const auto& entry = kCommandPaletteCatalog[ranked[index].catalogIndex];
            const auto availability = commandPaletteAvailability(entry.command);
            std::string label{entry.name};
            if (!entry.shortcut.empty()) {
                label += "  ";
                label += entry.shortcut;
            }
            if (!availability.enabled && !availability.reason.empty()) {
                label += "  — ";
                label += availability.reason;
            }
            commandPaletteRows_.push_back(CommandPaletteRow{
                .command = entry.command,
                .enabled = availability.enabled,
                .label = std::move(label),
            });
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Command palette list allocation failed");
    }
    std::string summary;
    try {
        if (commandPaletteRows_.empty()) {
            summary = "No matching commands";
        } else {
            summary = std::to_string(commandPaletteRows_.size());
            summary += commandPaletteRows_.size() == 1U ? " command" : " commands";
            const auto selected = observedCommandPaletteSelectionIndex_.value_or(0U);
            if (selected < commandPaletteRows_.size() &&
                !commandPaletteRows_[static_cast<Tina::Core::usize>(selected)].enabled) {
                summary += " · selected command is unavailable";
            } else {
                summary += " · Enter runs the selected command";
            }
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Command palette summary allocation failed");
    }
    if (auto status = tree.setText(commandPaletteSummary_, summary); !status) {
        return status;
    }
    if (auto status =
            tree.setListViewDataSource(commandPaletteList_, commandPaletteDataSource());
        !status) {
        return status;
    }
    if (auto status = tree.invalidateListViewItems(commandPaletteList_); !status) {
        return status;
    }
    if (!commandPaletteRows_.empty()) {
        u64 selected = observedCommandPaletteSelectionIndex_.value_or(0U);
        if (selected >= commandPaletteRows_.size()) {
            selected = 0U;
        }
        if (auto status = tree.setListViewSelectedIndex(commandPaletteList_, selected);
            !status) {
            return status;
        }
        observedCommandPaletteSelectionIndex_ = selected;
        const bool selectedEnabled =
            commandPaletteRows_[static_cast<Tina::Core::usize>(selected)].enabled;
        if (auto status = tree.setEnabled(
                commandPaletteDialog_.actions[CommandPaletteRunActionIndex],
                selectedEnabled);
            !status) {
            return status;
        }
    } else {
        observedCommandPaletteSelectionIndex_.reset();
        if (auto status = tree.clearListViewSelection(commandPaletteList_); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                commandPaletteDialog_.actions[CommandPaletteRunActionIndex], false);
            !status) {
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateCommandPaletteSearch(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!commandPaletteOpen_) {
        return Tina::Core::success();
    }
    auto text = tree.text(commandPaletteSearchInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (*text == commandPaletteFilterUtf8_) {
        return Tina::Core::success();
    }
    try {
        commandPaletteFilterUtf8_.assign(text->data(), text->size());
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Command palette search filter allocation failed");
    }
    observedCommandPaletteSelectionIndex_ = 0U;
    return refreshCommandPaletteUi(tree);
}

auto EditorWorkspaceState::processCommandPaletteListSelection(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!commandPaletteOpen_) {
        return Tina::Core::success();
    }
    if (pendingCommandPaletteKeyboardSelection_) {
        pendingCommandPaletteKeyboardSelection_ = false;
        return Tina::Core::success();
    }
    auto selection = tree.listViewSelection(commandPaletteList_);
    if (!selection) {
        return Tina::Core::failure(std::move(selection.error()));
    }
    if (!selection->hasValue()) {
        return Tina::Core::success();
    }
    if (observedCommandPaletteSelectionIndex_ == selection->logicalIndex) {
        return Tina::Core::success();
    }
    observedCommandPaletteSelectionIndex_ = selection->logicalIndex;
    return refreshCommandPaletteUi(tree);
}

auto EditorWorkspaceState::executeCommandPaletteSelection(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!commandPaletteOpen_ || commandPaletteRows_.empty()) {
        return hideCommandPalette(tree);
    }
    const u64 selected = observedCommandPaletteSelectionIndex_.value_or(0U);
    if (selected >= commandPaletteRows_.size()) {
        return hideCommandPalette(tree);
    }
    const CommandPaletteRow& row =
        commandPaletteRows_[static_cast<Tina::Core::usize>(selected)];
    if (!row.enabled) {
        authoringFeedback_ = "Command palette: the selected command is unavailable";
        return Tina::Core::success();
    }
    const EditorCommand command = row.command;
    if (auto status = hideCommandPalette(tree); !status) {
        return status;
    }
    pendingCommandPaletteRun_ = command;
    return Tina::Core::success();
}

auto EditorWorkspaceState::commandPaletteDataSource() const noexcept
    -> UI::UIListViewDataSource
{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::commandPaletteItemCount,
        .resolveItem = &EditorWorkspaceState::resolveCommandPaletteItem,
    };
}

auto EditorWorkspaceState::commandPaletteItemCount(const void* state) noexcept -> u64
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->commandPaletteRows_.size() : 0U;
}

auto EditorWorkspaceState::resolveCommandPaletteItem(
    const void* state, u64 logicalIndex,
    UI::UIListViewItemDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalIndex >= self->commandPaletteRows_.size()) {
        return false;
    }
    const auto& row =
        self->commandPaletteRows_[static_cast<Tina::Core::usize>(logicalIndex)];
    output = UI::UIListViewItemDescriptor{
        .key = logicalIndex + 1U,
        .label = row.label,
        .enabled = true,
    };
    return true;
}

} // namespace Tina::EditorApp::WorkspaceInternal
