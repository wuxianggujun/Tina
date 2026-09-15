#include "EditorWorkspaceState.hpp"

#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <array>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] constexpr Tina::Core::u64 autosaveMaximumBytes() noexcept
{
    return 16U * 1024U * 1024U;
}

[[nodiscard]] std::string autosaveStem(Tina::Editor::EditorDocumentKey key)
{
    switch (key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return "world2d";
    case Tina::Editor::EditorDocumentKind::World3D:
        return "world3d";
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        if (!key.assetId) {
            return {};
        }
        {
            const auto text = key.assetId.canonicalText();
            return std::string{"anim-"} + std::string{text.data(), text.size()};
        }
    case Tina::Editor::EditorDocumentKind::Fx2D:
        if (!key.assetId) {
            return {};
        }
        {
            const auto text = key.assetId.canonicalText();
            return std::string{"fx2d-"} + std::string{text.data(), text.size()};
        }
    case Tina::Editor::EditorDocumentKind::TileMap2D:
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return {};
    }
}

[[nodiscard]] bool fileIsNewer(const std::filesystem::path& candidate,
                               const std::filesystem::path& baseline) noexcept
{
    std::error_code candidateError;
    std::error_code baselineError;
    const auto candidateTime = std::filesystem::last_write_time(candidate, candidateError);
    if (candidateError) {
        return false;
    }
    const auto baselineTime = std::filesystem::last_write_time(baseline, baselineError);
    if (baselineError) {
        return true;
    }
    return candidateTime > baselineTime;
}

} // namespace

auto EditorWorkspaceState::autosaveIdleFrameAvailable() const noexcept -> bool
{
    if (!editorSettings_.autosaveEnabled || options_.autoDemo ||
        playSessionActive() || !activeProjectWorkspace_.has_value() ||
        pendingProjectSwitch_.has_value()) {
        return false;
    }
    if (pendingEditorCommand_.has_value() || pendingDirtyCloseKey_.has_value() ||
        pendingSceneAddRequest_.has_value() ||
        pendingSceneDeleteConfirmation_.has_value() ||
        pendingProjectAssetRemoveConfirmation_.has_value() ||
        spriteAssetPickerVisible_ || autosaveRestoreDialogVisible_) {
        return false;
    }
    return sourceImportService_.state() ==
           Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
}

auto EditorWorkspaceState::tickDocumentAutosave() noexcept -> void
{
    if (!autosaveIdleFrameAvailable()) {
        return;
    }
    const auto now = autosaveClock_.now();
    if (autosaveEpoch_ == Tina::Core::MonotonicTimePoint{}) {
        autosaveEpoch_ = now;
    }
    const double intervalSeconds =
        static_cast<double>(editorSettings_.autosaveMinutes) * 60.0;
    const auto directory = editorAutosaveDirectory(*activeProjectWorkspace_);
    std::error_code canonicalError;
    const auto projectRoot = std::filesystem::u8path(
        activeProjectWorkspace_->projectRootUtf8().begin(),
        activeProjectWorkspace_->projectRootUtf8().end());
    const auto physicalProject =
        std::filesystem::weakly_canonical(projectRoot, canonicalError);
    if (canonicalError) {
        authoringFeedback_ = "Autosave failed: project root could not be resolved";
        return;
    }
    std::error_code createError;
    (void)std::filesystem::create_directories(directory, createError);
    if (createError) {
        authoringFeedback_ = "Autosave failed: cache directory could not be created";
        return;
    }
    const auto physicalDirectory =
        std::filesystem::weakly_canonical(directory, canonicalError);
    if (canonicalError ||
        !pathIsSameOrDescendant(physicalDirectory, physicalProject)) {
        authoringFeedback_ = "Autosave failed: cache path escaped the project root";
        return;
    }

    const auto consider = [&](Tina::Editor::EditorDocumentKey key, bool dirty,
                              std::span<const std::byte> bytes) {
        if (!dirty || bytes.empty()) {
            return;
        }
        const std::string stem = autosaveStem(key);
        if (stem.empty()) {
            return;
        }
        DocumentAutosaveRecord* slot = nullptr;
        DocumentAutosaveRecord* empty = nullptr;
        for (auto& record : autosaveRecords_) {
            if (record.occupied && record.key == key) {
                slot = &record;
                break;
            }
            if (empty == nullptr && !record.occupied) {
                empty = &record;
            }
        }
        if (slot == nullptr) {
            if (empty == nullptr) {
                return;
            }
            empty->key = key;
            empty->occupied = true;
            slot = empty;
        }
        const auto elapsed = slot->written
                                 ? Tina::Core::durationBetween(slot->lastWrite, now)
                                 : Tina::Core::durationBetween(autosaveEpoch_, now);
        if (elapsed.count() + 0.001 < intervalSeconds) {
            return;
        }
        const auto path = directory / stem;
        const auto physicalPath =
            std::filesystem::weakly_canonical(path, canonicalError);
        if (canonicalError ||
            !pathIsSameOrDescendant(physicalPath.parent_path(), physicalDirectory)) {
            authoringFeedback_ = "Autosave failed: document key escaped the cache";
            return;
        }
        auto status = Tina::Core::writeFile(
            pathToUtf8(path), bytes,
            Tina::Core::WriteFileConfig{.atomicReplace = true, .createParents = true});
        if (!status) {
            authoringFeedback_ = "Autosave failed: ";
            authoringFeedback_ += status.error().message;
            return;
        }
        slot->written = true;
        slot->lastWrite = now;
    };

    consider({.kind = Tina::Editor::EditorDocumentKind::World2D},
             isDocumentDirty(WorkspaceMode::World2D), document_.snapshotBytes());
    consider({.kind = Tina::Editor::EditorDocumentKind::World3D},
             isDocumentDirty(WorkspaceMode::World3D), document3D_.payloadBytes());
    if (fx2DDocumentOwnerKey_.assetId) {
        const auto* session = findDocumentSession(fx2DDocumentOwnerKey_);
        consider(fx2DDocumentOwnerKey_,
                 session != nullptr &&
                     !savedBaselineMatches(session->savedBaseline, fx2DDocument_),
                 fx2DDocument_.payloadBytes());
    }
}

auto EditorWorkspaceState::clearDocumentAutosave(
    Tina::Editor::EditorDocumentKey key) noexcept -> void
{
    if (!activeProjectWorkspace_.has_value()) {
        return;
    }
    const std::string stem = autosaveStem(key);
    if (stem.empty()) {
        return;
    }
    const auto path = editorAutosaveDirectory(*activeProjectWorkspace_) / stem;
    std::error_code removeError;
    (void)std::filesystem::remove(path, removeError);
    for (auto& record : autosaveRecords_) {
        if (record.key == key) {
            record = {};
        }
    }
}

auto EditorWorkspaceState::queueAutosaveRestoreScan() noexcept -> void
{
    pendingAutosaveRestoreKey_.reset();
    if (!activeProjectWorkspace_.has_value() || options_.autoDemo ||
        !editorSettings_.autosaveEnabled) {
        return;
    }
    const auto alreadyPrompted = [&](Tina::Editor::EditorDocumentKey key) noexcept {
        for (u32 index = 0; index < autosavePromptedCount_; ++index) {
            if (autosavePromptedKeys_[index] == key) {
                return true;
            }
        }
        return false;
    };
    const auto directory = editorAutosaveDirectory(*activeProjectWorkspace_);
    const std::array candidates{
        Tina::Editor::EditorDocumentKey{
            .kind = Tina::Editor::EditorDocumentKind::World2D},
        Tina::Editor::EditorDocumentKey{
            .kind = Tina::Editor::EditorDocumentKind::World3D},
        fx2DDocumentOwnerKey_,
    };
    for (const auto& key : candidates) {
        const std::string stem = autosaveStem(key);
        if (stem.empty() || alreadyPrompted(key)) {
            continue;
        }
        const auto autosavePath = directory / stem;
        std::error_code existsError;
        if (!std::filesystem::is_regular_file(
                std::filesystem::symlink_status(autosavePath, existsError))) {
            continue;
        }
        const auto* session = findDocumentSession(key);
        if (session != nullptr && session->hasDocumentPath()) {
            const auto documentPath = std::filesystem::u8path(
                session->documentPathUtf8.begin(), session->documentPathUtf8.end());
            if (!fileIsNewer(autosavePath, documentPath)) {
                continue;
            }
        }
        pendingAutosaveRestoreKey_ = key;
        break;
    }
}

auto EditorWorkspaceState::processPendingAutosaveRestore(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingAutosaveRestoreKey_.has_value() || autosaveRestoreDialogVisible_) {
        return Tina::Core::success();
    }
    if (pendingDirtyCloseKey_.has_value() || pendingSceneAddRequest_.has_value() ||
        pendingSceneDeleteConfirmation_.has_value() ||
        pendingProjectAssetRemoveConfirmation_.has_value() ||
        spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    return showAutosaveRestoreModal(tree);
}

auto EditorWorkspaceState::showAutosaveRestoreModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingAutosaveRestoreKey_.has_value()) {
        return Tina::Core::success();
    }
    const std::string stem = autosaveStem(*pendingAutosaveRestoreKey_);
    std::string message = "A newer autosave exists for ";
    message += stem.empty() ? "the document" : stem;
    message += ". Restore it, or discard the backup?";
    if (auto status = tree.setText(autosaveRestoreMessage_, message); !status) {
        return status;
    }
    if (auto status = tree.openDialog(autosaveRestoreDialog_.modal); !status) {
        return status;
    }
    autosaveRestoreDialogVisible_ = true;
    pendingAutosaveRestoreDialogFocus_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideAutosaveRestoreModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    pendingAutosaveRestoreDialogFocus_ = false;
    autosaveRestoreDialogVisible_ = false;
    pendingAutosaveRestoreKey_.reset();
    return tree.dismissDialog(autosaveRestoreDialog_.modal);
}

auto EditorWorkspaceState::confirmAutosaveRestore(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingAutosaveRestoreKey_.has_value() ||
        !activeProjectWorkspace_.has_value()) {
        return hideAutosaveRestoreModal(tree);
    }
    const auto key = *pendingAutosaveRestoreKey_;
    const std::string stem = autosaveStem(key);
    const auto path = editorAutosaveDirectory(*activeProjectWorkspace_) / stem;
    auto bytes = Tina::Core::readFile(
        pathToUtf8(path), Tina::Core::ReadFileConfig{.maxBytes = autosaveMaximumBytes()});
    if (!bytes) {
        authoringFeedback_ = "Autosave restore failed: ";
        authoringFeedback_ += bytes.error().message;
        return hideAutosaveRestoreModal(tree);
    }
    Tina::Core::Status loaded = Tina::Core::success();
    switch (key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        loaded = document_.loadSnapshot(*bytes);
        break;
    case Tina::Editor::EditorDocumentKind::World3D:
        loaded = document3D_.loadPayload(*bytes);
        break;
    case Tina::Editor::EditorDocumentKind::Fx2D: {
        auto parsed = Tina::AssetFormat::parseFx2DPayloadBytes(*bytes);
        if (!parsed) {
            loaded = Tina::Core::failure(std::move(parsed.error()));
            break;
        }
        auto restored = Tina::Editor::Fx2DAuthoringDocument::Create(
            *parsed, fx2DDocument_.config());
        if (!restored) {
            loaded = Tina::Core::failure(std::move(restored.error()));
            break;
        }
        fx2DDocument_ = std::move(*restored);
        fxPreview_.reset();
        fxPreviewRevision_ = 0;
        break;
    }
    default:
        loaded = Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Autosave restore is not available for this document kind");
        break;
    }
    if (!loaded) {
        authoringFeedback_ = "Autosave restore rejected: ";
        authoringFeedback_ += loaded.error().message;
        return hideAutosaveRestoreModal(tree);
    }
    if (autosavePromptedCount_ < autosavePromptedKeys_.size()) {
        autosavePromptedKeys_[autosavePromptedCount_++] = key;
    }
    authoringFeedback_ = "Autosave restored; save the document to keep the recovery";
    if (auto status = hideAutosaveRestoreModal(tree); !status) {
        return status;
    }
    queueAutosaveRestoreScan();
    if (auto preview = validateRuntimePreview(); !preview) {
        authoringFeedback_ += " | Preview rebuild pending";
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::confirmAutosaveDiscard(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingAutosaveRestoreKey_.has_value()) {
        if (autosavePromptedCount_ < autosavePromptedKeys_.size()) {
            autosavePromptedKeys_[autosavePromptedCount_++] = *pendingAutosaveRestoreKey_;
        }
        clearDocumentAutosave(*pendingAutosaveRestoreKey_);
        authoringFeedback_ = "Autosave discarded";
    }
    if (auto status = hideAutosaveRestoreModal(tree); !status) {
        return status;
    }
    queueAutosaveRestoreScan();
    return refreshAuthoringUi(tree);
}

} // namespace Tina::EditorApp::WorkspaceInternal
