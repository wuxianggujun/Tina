#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::animationModeLabel(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept -> std::string_view{
    switch (mode) {
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
        return "Once";
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
        return "PingPong";
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
    default:
        return "Loop";
    }
}

auto EditorWorkspaceState::applyAnimationPreviewFrame(u32 frameIndex) -> Tina::Core::Status{
    const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
    if (!frame) {
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                   "Animation timeline frame does not exist");
    }
    animationPreview_.setSelectedFrameIndex(frameIndex);
    counters_.animationPreviewFrameIndex = frameIndex;
    const Tina::Asset::AssetHandle sprite = loadedAsset(
        frame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
    if (!sprite || !containsHandle(boundSpriteAssets_, sprite)) {
        animationPreview_.setPreviewAvailable(false);
        return Tina::Core::success();
    }
    animationPreview_.setPreviewAvailable(true);
    if (workspaceMode_ != WorkspaceMode::World2D || !previewWorld_.has_value()) {
        return Tina::Core::success();
    }
    const Tina::Scene::World2DEntityBinding* target = findPreviewBinding(selectionKey_);
    if (target == nullptr || previewWorld_->spriteRenderer2D(target->entity) == nullptr) {
        const auto firstSprite = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [this](const Tina::Scene::World2DEntityBinding& binding) {
                return previewWorld_->spriteRenderer2D(binding.entity) != nullptr;
            });
        target = firstSprite != previewBindings_.end() ? &*firstSprite : nullptr;
    }
    if (target == nullptr) {
        animationPreview_.setPreviewAvailable(false);
        return Tina::Core::success();
    }
    const Tina::Scene::SpriteRenderer2D* current =
        previewWorld_->spriteRenderer2D(target->entity);
    Tina::Scene::SpriteRenderer2D updated = *current;
    updated.sprite = sprite;
    return previewWorld_->setSpriteRenderer2D(target->entity, updated);
}

auto EditorWorkspaceState::rebuildAnimationAnimator() -> Tina::Core::Status{
    if (auto status = animationPreview_.rebuild(
            spriteAnimationDocument_, assetResources_.memory,
            [this](Tina::Core::AssetId spriteId) {
                const Tina::Asset::AssetHandle sprite = loadedAsset(
                    spriteId, Tina::AssetFormat::AssetKind::Sprite);
                return sprite && containsHandle(boundSpriteAssets_, sprite)
                           ? sprite
                           : Tina::Asset::AssetHandle{};
            });
        !status) {
        return status;
    }
    if (!animationPreview_.hasAnimator()) {
        return Tina::Core::success();
    }
    return applyAnimationPreviewFrame(animationPreview_.selectedFrameIndex());
}

auto EditorWorkspaceState::processPendingAnimationFrameSelection(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const auto pendingSlot = animationPreview_.takePendingFrameSelection();
    if (!pendingSlot.has_value()) {
        return Tina::Core::success();
    }
    const u32 slot = *pendingSlot;
    if (workspaceMode_ != WorkspaceMode::World2D) {
        return refreshAnimationTimelineUi(tree);
    }
    const u32 frameIndex = animationPreview_.visibleFrameStart() + slot;
    if (frameIndex >= spriteAnimationDocument_.frameCount()) {
        return Tina::Core::success();
    }
    animationPreview_.setPlaying(false);
    if (animationPreview_.hasAnimator()) {
        animationPreview_.animator().pause();
    }
    if (auto status = applyAnimationPreviewFrame(frameIndex); !status) {
        return status;
    }
    authoringFeedback_ = "Animation playhead moved to frame " +
                         std::to_string(frameIndex + 1U);
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::refreshAnimationTimelineUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const bool editable = workspaceMode_ == WorkspaceMode::World2D &&
                          authoringEnabled();
    const u32 frameCount = static_cast<u32>(spriteAnimationDocument_.frameCount());
    animationPreview_.clampSelection(frameCount);
    counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
    counters_.animationFrameCount = frameCount;
    counters_.animationPreviewFrameIndex = animationPreview_.selectedFrameIndex();

    std::string statusText;
    if (!editable) {
        statusText = "Switch to 2D to edit this clip";
    } else {
        statusText = std::to_string(frameCount);
        statusText += " frames | ";
        statusText += std::to_string(static_cast<u64>(std::llround(
            spriteAnimationDocument_.totalDurationSeconds() * 1000.0)));
        statusText += " ms | Rev ";
        statusText += std::to_string(spriteAnimationDocument_.revision());
        statusText += " | Cook ";
        statusText += std::to_string(counters_.animationCookPreviewBytes);
        statusText += " B";
        if (!animationPreview_.previewAvailable()) {
            statusText += " | Sprite unresolved";
        }
    }
    if (auto status = tree.setText(animationStatus_, statusText); !status) {
        return status;
    }
    std::string modeText = "Mode: ";
    modeText += animationModeLabel(spriteAnimationDocument_.playbackMode());
    if (auto status = tree.setText(animationModeButton_, modeText); !status) {
        return status;
    }
    if (auto status = tree.setText(animationPlayButton_, animationPreview_.playing() ? "Pause" : "Play");
        !status) {
        return status;
    }

    const auto selectedFrame = spriteAnimationDocument_.frameAt(animationPreview_.selectedFrameIndex());
    if (!selectedFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Animation timeline selected frame is invalid");
    }
    const auto spriteText = selectedFrame->spriteId.canonicalText();
    std::string selectionText = "Frame ";
    selectionText += std::to_string(animationPreview_.selectedFrameIndex() + 1U);
    selectionText += " | Sprite ";
    selectionText.append(spriteText.data(),
                         (std::min)(Tina::Core::usize{8}, spriteText.size()));
    selectionText += " | ";
    selectionText += std::to_string(
        static_cast<u32>(std::lround(selectedFrame->durationSeconds * 1000.0F)));
    selectionText += " ms";
    if (auto status = tree.setText(animationSelection_, selectionText); !status) {
        return status;
    }

    animationPreview_.setVisibleFrameStart(0);
    if (frameCount > AnimationVisibleFrameSlots &&
        animationPreview_.selectedFrameIndex() >= AnimationVisibleFrameSlots) {
        animationPreview_.setVisibleFrameStart((std::min)(
            animationPreview_.selectedFrameIndex() - AnimationVisibleFrameSlots + 1U,
            frameCount - AnimationVisibleFrameSlots));
    }
    for (u32 slot = 0; slot < animationFrameButtons_.size(); ++slot) {
        const u32 frameIndex = animationPreview_.visibleFrameStart() + slot;
        const bool materialized = frameIndex < frameCount;
        std::string label = "--";
        if (materialized) {
            const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
            label = frameIndex == animationPreview_.selectedFrameIndex() ? ">" : "";
            label += std::to_string(frameIndex + 1U);
            label += " ";
            label += std::to_string(
                static_cast<u32>(std::lround(frame->durationSeconds * 1000.0F)));
            label += "ms";
        }
        if (auto status = tree.setText(animationFrameButtons_[slot], label); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationFrameButtons_[slot], editable && materialized);
            !status) {
            return status;
        }
    }

    if (auto status = tree.setEnabled(animationPlayButton_, editable && animationPreview_.previewAvailable());
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationPreviousButton_,
                                      editable && animationPreview_.selectedFrameIndex() > 0U);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationNextButton_,
                                      editable && animationPreview_.selectedFrameIndex() + 1U < frameCount);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationAddButton_,
                                      editable && frameCount < spriteAnimationDocument_.config().frameCapacity);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationDuplicateButton_, editable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationDeleteButton_, editable && frameCount > 1U); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationMoveLeftButton_,
                                      editable && animationPreview_.selectedFrameIndex() > 0U);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationMoveRightButton_,
                                      editable && animationPreview_.selectedFrameIndex() + 1U < frameCount);
        !status) {
        return status;
    }
    const std::array alwaysEditableButtons{
        animationCycleSpriteButton_, animationDurationDecreaseButton_,
        animationDurationIncreaseButton_,
        animationModeButton_, animationCookButton_,
    };
    for (const UI::UINodeId button : alwaysEditableButtons) {
        if (auto status = tree.setEnabled(button, editable); !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(animationUndoButton_,
                                      editable && spriteAnimationDocument_.canUndo());
        !status) {
        return status;
    }
    return tree.setEnabled(animationRedoButton_,
                           editable && spriteAnimationDocument_.canRedo());
}

} // namespace Tina::EditorApp::WorkspaceInternal
