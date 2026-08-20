#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] u32 hashAnimationEventTag(std::string_view name) noexcept
{
    constexpr u32 OffsetBasis = 2166136261U;
    constexpr u32 Prime = 16777619U;
    u32 hash = OffsetBasis;
    for (const char character : name)
    {
        hash ^= static_cast<u32>(static_cast<unsigned char>(character));
        hash *= Prime;
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] bool isAnimationEventIdentifier(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 64U ||
        (text.front() >= '0' && text.front() <= '9'))
    {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char character) noexcept {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] std::string animationEventTagText(u32 tag)
{
    constexpr char Digits[] = "0123456789ABCDEF";
    std::string text{"0x00000000"};
    for (u32 digit = 0; digit < 8U; ++digit)
    {
        text[9U - digit] = Digits[(tag >> (digit * 4U)) & 0xFU];
    }
    return text;
}

} // namespace

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
    animationSelectedEventIndex_ = 0U;
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

auto EditorWorkspaceState::readAnimationEventInput(
    Tina::PrimaryWindowUITreeUpdater& tree) const
    -> Tina::Core::Result<Tina::AssetFormat::SpriteAnimationEventDesc>
{
    auto tagText = tree.text(animationEventTag_);
    if (!tagText)
    {
        return Tina::Core::failure(std::move(tagText.error()));
    }
    auto offsetText = tree.text(animationEventOffset_);
    if (!offsetText)
    {
        return Tina::Core::failure(std::move(offsetText.error()));
    }

    u32 eventTag = 0;
    if (tagText->starts_with("0x") || tagText->starts_with("0X"))
    {
        const std::string_view digits = tagText->substr(2U);
        if (digits.empty() || digits.size() > 8U)
        {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Animation notify tag must be 0x plus 1 to 8 hexadecimal digits");
        }
        const auto [end, error] = std::from_chars(
            digits.data(), digits.data() + digits.size(), eventTag, 16);
        if (error != std::errc{} || end != digits.data() + digits.size())
        {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Animation notify tag contains invalid hexadecimal digits");
        }
    }
    else if (isAnimationEventIdentifier(*tagText))
    {
        eventTag = hashAnimationEventTag(*tagText);
    }
    else
    {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Animation notify tag must be hexadecimal or an ASCII identifier");
    }
    if (eventTag == 0U)
    {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Animation notify tag must be non-zero");
    }

    bool percentage = false;
    std::string_view numericText = *offsetText;
    if (!numericText.empty() && numericText.back() == '%')
    {
        percentage = true;
        numericText.remove_suffix(1U);
    }
    if (numericText.empty())
    {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Animation notify offset must be a decimal or percentage");
    }
    std::string numericBuffer{numericText};
    errno = 0;
    char* end = nullptr;
    float normalizedOffset = std::strtof(numericBuffer.c_str(), &end);
    if (errno != 0 || end == numericBuffer.c_str() || *end != '\0')
    {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Animation notify offset must be a decimal or percentage");
    }
    if (percentage)
    {
        normalizedOffset /= 100.0F;
    }
    if (!std::isfinite(normalizedOffset) || normalizedOffset < 0.0F ||
        normalizedOffset > 1.0F)
    {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Animation notify offset must be finite and within [0, 1]");
    }
    return Tina::AssetFormat::SpriteAnimationEventDesc{
        .eventTag = eventTag,
        .normalizedOffset = normalizedOffset,
    };
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
    selectionText += " | Events ";
    selectionText += std::to_string(selectedFrame->events.size());
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
        const bool selected = materialized &&
                              frameIndex == animationPreview_.selectedFrameIndex();
        std::string label = "--";
        if (materialized) {
            label = std::to_string(frameIndex + 1U);
        }
        if (auto status = tree.setText(animationFrameButtons_[slot], label); !status) {
            return status;
        }
        if (auto status = tree.setStyleRole(
                animationFrameButtons_[slot],
                selected ? UI::UIStyleRoleId::ButtonPrimary
                         : UI::UIStyleRoleId::ButtonOutlined);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationFrameButtons_[slot], editable && materialized);
            !status) {
            return status;
        }
    }

    const u32 eventCount = static_cast<u32>(selectedFrame->events.size());
    animationSelectedEventIndex_ = eventCount == 0U
                                       ? 0U
                                       : (std::min)(animationSelectedEventIndex_, eventCount - 1U);
    counters_.animationEventCount = eventCount;
    counters_.animationSelectedEventIndex = animationSelectedEventIndex_;
    std::string eventPositionText = "Notify ";
    eventPositionText += std::to_string(
        eventCount == 0U ? 0U : animationSelectedEventIndex_ + 1U);
    eventPositionText += "/";
    eventPositionText += std::to_string(eventCount);
    if (auto status = tree.setText(animationEventPosition_, eventPositionText);
        !status)
    {
        return status;
    }
    if (eventCount != 0U)
    {
        const auto& selectedEvent = selectedFrame->events[animationSelectedEventIndex_];
        if (auto status = tree.setText(animationEventTag_,
                                       animationEventTagText(selectedEvent.eventTag));
            !status)
        {
            return status;
        }
        auto offsetText = formatEditorNumber(selectedEvent.normalizedOffset);
        if (!offsetText)
        {
            return Tina::Core::failure(std::move(offsetText.error()));
        }
        if (auto status = tree.setText(animationEventOffset_, offsetText->view());
            !status)
        {
            return status;
        }
    }
    if (auto status = tree.setEnabled(animationEventTag_, editable); !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventOffset_, editable); !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventPreviousButton_,
                                      editable && animationSelectedEventIndex_ > 0U);
        !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventNextButton_,
                                      editable && animationSelectedEventIndex_ + 1U < eventCount);
        !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(
            animationEventAddButton_,
            editable && eventCount <
                            Tina::AssetFormat::SpriteAnimationClipWire::MaxEventsPerFrame);
        !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventApplyButton_, editable && eventCount != 0U);
        !status)
    {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventRemoveButton_, editable && eventCount != 0U);
        !status)
    {
        return status;
    }

    UI::UILayoutStyle playButtonLayout = animationPlaybackButtons_.layout;
    playButtonLayout.visibility = animationPreview_.playing()
                                      ? UI::UIVisibility::Collapsed
                                      : UI::UIVisibility::Visible;
    if (auto status = tree.setLayoutStyle(
            animationPlaybackButtons_.play.root, playButtonLayout);
        !status) {
        return status;
    }
    UI::UILayoutStyle pauseButtonLayout = animationPlaybackButtons_.layout;
    pauseButtonLayout.visibility = animationPreview_.playing()
                                       ? UI::UIVisibility::Visible
                                       : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            animationPlaybackButtons_.pause.root, pauseButtonLayout);
        !status) {
        return status;
    }
    const bool playbackEnabled = editable && animationPreview_.previewAvailable();
    if (auto status = tree.setEnabled(
            animationPlaybackButtons_.play.button, playbackEnabled);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            animationPlaybackButtons_.pause.button, playbackEnabled);
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
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
