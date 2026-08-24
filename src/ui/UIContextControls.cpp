#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Status UIContext::Impl::setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                      UIButtonActionCallback&& callback)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto buttonResult = resolveButton(button);
    if (!buttonResult)
    {
        return Core::failure(buttonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
    }
    if (!callback.hasValue())
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Button action callback is empty");
    }

    auto registration = buttonActionRegistry.stage(
        button, std::move(callback), routeDispatchDepth != 0);
    if (!registration)
    {
        return Core::failure(registration.error());
    }

    const auto rollbackRegistration = [&](Core::Error error) {
        buttonActionRegistry.rollback(*registration, routeDispatchDepth != 0);
        return Core::failure(std::move(error));
    };

    if (!contains(updaterRoot))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::RootRequired,
            "UI tree updater root was released while setting a Button action"));
    }
    auto liveButtonResult = resolveButton(button);
    if (!liveButtonResult)
    {
        return rollbackRegistration(liveButtonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return rollbackRegistration(
            makeError(UIErrorCode::InvalidNode, "UI Button left the updater root while setting its action"));
    }
    if (!buttonActionRegistry.canCommit(*registration))
    {
        return rollbackRegistration(
            makeError(UIErrorCode::InvalidButtonAction, "UI Button action changed during callback transfer"));
    }
    Core::Status committed = buttonActionRegistry.commit(
        *registration, routeDispatchDepth != 0);
    if (!committed)
    {
        return rollbackRegistration(committed.error());
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto buttonResult = resolveButton(button);
    if (!buttonResult)
    {
        return Core::failure(buttonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
    }

    buttonActionRegistry.clear(
        button, routeDispatchDepth != 0 ? buttonRouteSerial : 0, routeDispatchDepth != 0);
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto buttonResult = resolveButton(button);
    if (!buttonResult)
    {
        return Core::failure(buttonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
    }
    return isButtonPressed(button);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveCheckbox(UINodeId checkbox)
{
    auto nodeResult = resolveNode(checkbox);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Checkbox)
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Checkbox API requires a Checkbox node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveToggle(UINodeId node)
{
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!behaviorStateStorage.hasToggle(node.index()))
    {
        return fail(UIErrorCode::InvalidButtonAction,
                    "UI checked state requires a Toggle-capable Element");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                        UIButtonActionCallback&& callback)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    auto checkboxResult = resolveCheckbox(checkbox);
    if (!checkboxResult)
    {
        return Core::failure(checkboxResult.error());
    }
    return setButtonActionFromUpdater(updaterRoot, checkbox, std::move(callback));
}

[[nodiscard]] Core::Status UIContext::Impl::clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    auto checkboxResult = resolveCheckbox(checkbox);
    if (!checkboxResult)
    {
        return Core::failure(checkboxResult.error());
    }
    return clearButtonActionFromUpdater(updaterRoot, checkbox);
}

[[nodiscard]] Core::Status UIContext::Impl::setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                       const UICheckboxPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto checkboxResult = resolveCheckbox(checkbox);
    if (!checkboxResult)
    {
        return Core::failure(checkboxResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, checkbox))
    {
        return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
    }
    if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
    }
    if (!std::isfinite(paint.checkedIndicatorInset) || paint.checkedIndicatorInset < 0.0F ||
        paint.presentation < UIToggleIndicatorPresentation::Checkbox ||
        paint.presentation > UIToggleIndicatorPresentation::Switch)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Checkbox paint inset or indicator presentation is invalid");
    }
    UICheckboxPaint& currentPaint = checkboxPaintsByNodeIndex[checkbox.index()];
    if (currentPaint == paint)
    {
        if ((styleOverridesByNodeIndex[checkbox.index()] &
             static_cast<u16>(UIStyleOverride::CheckboxPaint)) != 0)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
        {
            return dirty;
        }
        detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
    {
        return dirty;
    }
    currentPaint = paint;
    detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UICheckboxPaint> UIContext::Impl::checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto checkboxResult = const_cast<Impl*>(this)->resolveCheckbox(checkbox);
    if (!checkboxResult)
    {
        return Core::failure(checkboxResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, checkbox))
    {
        return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
    }
    if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
    }
    return checkboxPaintsByNodeIndex[checkbox.index()];
}

[[nodiscard]] Core::Status UIContext::Impl::setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto toggleResult = resolveToggle(checkbox);
    if (!toggleResult)
    {
        return Core::failure(toggleResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, checkbox))
    {
        return fail(UIErrorCode::InvalidNode, "UI Toggle element is not owned by the updater root");
    }
    u8* currentValue = behaviorStateStorage.tryToggleValue(checkbox.index());
    if (currentValue == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Toggle state index is out of range");
    }
    const u8 next = checked ? 1 : 0;
    if (*currentValue != next)
    {
        if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
        {
            return dirty;
        }
        *currentValue = next;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    // const_cast: resolveToggle is non-const only for API reuse of resolveNode.
    auto toggleResult = const_cast<Impl*>(this)->resolveToggle(checkbox);
    if (!toggleResult)
    {
        return Core::failure(toggleResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, checkbox))
    {
        return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
    }
    const u8* currentValue = behaviorStateStorage.tryToggleValue(checkbox.index());
    if (currentValue == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Toggle state index is out of range");
    }
    return *currentValue != 0;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto checkboxResult = resolveCheckbox(checkbox);
    if (!checkboxResult)
    {
        return Core::failure(checkboxResult.error());
    }
    return isButtonPressedFromUpdater(updaterRoot, checkbox);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveSlider(UINodeId slider)
{
    auto nodeResult = resolveNode(slider);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Slider)
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Slider API requires a Slider node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveRangeInput(UINodeId node)
{
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!hasBehavior((*nodeResult)->behaviors, UIElementBehavior::RangeInput) ||
        behaviorStateStorage.tryRangeInputState(node.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI RangeInput API requires a RangeInput-capable node");
    }
    return *nodeResult;
}

[[nodiscard]] bool UIContext::Impl::isInteractiveRangeInput(UINodeId node) const noexcept
{
    if (!node.hasValue() || node.index() >= semanticsStatesByNodeIndex.size() || !isNodeEnabled(node) ||
        behaviorStateStorage.tryRangeInputState(node.index()) == nullptr)
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    return record != nullptr && hasBehavior(record->behaviors, UIElementBehavior::RangeInput) &&
           !semanticsStatesByNodeIndex[node.index()].readOnly;
}

[[nodiscard]] bool UIContext::Impl::isPointerAdjustableRangeInput(UINodeId node) const noexcept
{
    if (!isInteractiveRangeInput(node))
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    return record != nullptr &&
           (record->kind == BuiltinElementKind::Slider ||
            (record->kind == BuiltinElementKind::Splitter &&
             splitViewStorage.splitViewForSplitter(node).hasValue()));
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applySliderValue(UINodeId slider, double requestedValue,
                                                   Platform::PlatformFrameId platformFrame,
                                                   u64 sourceSequence, bool requireInteractive,
                                                   bool quantizeToStep)
{
    if (!slider.hasValue())
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(slider.storageId());
    Detail::UIRangeInputState* state = behaviorStateStorage.tryRangeInputState(slider.index());
    if (record == nullptr || !hasBehavior(record->behaviors, UIElementBehavior::RangeInput) || state == nullptr)
    {
        return false;
    }
    if (requireInteractive && !isInteractiveRangeInput(slider))
    {
        return false;
    }
    if (!std::isfinite(requestedValue) || !std::isfinite(state->minValue) || !std::isfinite(state->maxValue) ||
        !(state->maxValue > state->minValue))
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Slider value update requires a finite range and value");
    }
    const float next = quantizeToStep
                           ? quantizeSliderValue(requestedValue, state->minValue,
                                                 state->maxValue, state->step)
                           : static_cast<float>(std::clamp(
                                 requestedValue, static_cast<double>(state->minValue),
                                 static_cast<double>(state->maxValue)));
    if (next == state->value)
    {
        return false;
    }
    const bool splitter = record->kind == BuiltinElementKind::Splitter;
    const UINodeId splitView = splitter ? splitViewStorage.splitViewForSplitter(slider)
                                        : UINodeId{};
    if (splitter && !splitView.hasValue())
    {
        return false;
    }
    Core::Status dirty = splitter ? markLayoutStyleDirty(splitView)
                                  : markPaintDirty(slider);
    if (!dirty)
    {
        return Core::failure(dirty.error());
    }
    state->value = next;
    if (splitter)
    {
        splitViewStorage.setRequestedFraction(splitView, next);
    }
    else
    {
        invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                         .sliderNode = slider,
                                                                         .value = next,
                                                                         .platformFrame = platformFrame,
                                                                         .sourceSequence = sourceSequence,
                                                                     });
    }
    return true;
}

// Map pointer X into [min,max] using last committed hit worldRect for the slider.
[[nodiscard]] Core::Result<bool> UIContext::Impl::applySliderValueFromPointer(UINodeId slider, UILogicalPoint position,
                                                             Platform::PlatformFrameId platformFrame,
                                                             u64 sourceSequence)
{
    if (!slider.hasValue() || slider.index() >= sliderPaintsByNodeIndex.size())
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(slider.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::Slider || !isInteractiveRangeInput(slider))
    {
        return false;
    }
    Detail::UIRangeInputState* state = behaviorStateStorage.tryRangeInputState(slider.index());
    if (state == nullptr || !(state->maxValue > state->minValue) || !std::isfinite(state->minValue) ||
        !std::isfinite(state->maxValue))
    {
        return false;
    }

    UILogicalRect worldRect{};
    bool foundRect = false;
    const UICommittedHitView hit = committedHit();
    for (const UICommittedHitEntry& entry : hit.entries())
    {
        if (entry.node == slider)
        {
            worldRect = entry.worldRect;
            foundRect = true;
            break;
        }
    }
    if (!foundRect || !(worldRect.width > 0.0F))
    {
        return false;
    }

    const auto next = resolveSliderValueFromPointer(
        worldRect, sliderPaintsByNodeIndex[slider.index()], position.x, state->minValue, state->maxValue,
        state->step);
    if (!next)
    {
        return false;
    }
    return applySliderValue(slider, *next, platformFrame, sourceSequence, true);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applySplitterValueFromPointer(
    UINodeId splitter, UILogicalPoint position,
    Platform::PlatformFrameId platformFrame, u64 sourceSequence)
{
    const UINodeId splitView = splitViewStorage.splitViewForSplitter(splitter);
    const SplitViewState* state = splitViewStorage.trySplitView(splitView);
    const Detail::UIRangeInputState* range =
        behaviorStateStorage.tryRangeInputState(splitter.index());
    if (state == nullptr || range == nullptr || !isInteractiveRangeInput(splitter))
    {
        return false;
    }
    const UISplitViewMetrics metrics = splitViewStorage.committedMetrics(splitView);
    if (metrics.splitterRect.width <= 0.0F || metrics.splitterRect.height <= 0.0F)
    {
        return false;
    }
    UILogicalRect contentRect{};
    if (metrics.orientation == UISplitViewOrientation::Horizontal)
    {
        contentRect = {
            metrics.primaryRect.x,
            metrics.primaryRect.y,
            metrics.primaryRect.width + metrics.splitterRect.width +
                metrics.secondaryRect.width,
            metrics.primaryRect.height,
        };
    }
    else
    {
        contentRect = {
            metrics.primaryRect.x,
            metrics.primaryRect.y,
            metrics.primaryRect.width,
            metrics.primaryRect.height + metrics.splitterRect.height +
                metrics.secondaryRect.height,
        };
    }
    const auto fraction = resolveSplitViewFractionFromPointer(
        contentRect, resolvedSplitViewConfig(state->config), position,
        splitterDragGrabOffset);
    if (!fraction.has_value())
    {
        return false;
    }
    return applySliderValue(splitter, *fraction, platformFrame, sourceSequence, true,
                            false);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyRangeInputValueFromPointer(
    UINodeId node, UILogicalPoint position,
    Platform::PlatformFrameId platformFrame, u64 sourceSequence)
{
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr)
    {
        return false;
    }
    if (record->kind == BuiltinElementKind::Splitter)
    {
        return applySplitterValueFromPointer(node, position, platformFrame, sourceSequence);
    }
    return applySliderValueFromPointer(node, position, platformFrame, sourceSequence);
}

[[nodiscard]] Detail::UITextEditVisualHit UIContext::Impl::textEditHitFromPointer(
    UINodeId textEdit, UILogicalPoint position) const noexcept
{
    if (!isLiveTextEdit(textEdit))
    {
        return {};
    }
    UICommittedContentPlacement placement{};
    bool foundPlacement = false;
    const UICommittedLayoutView layout = committedLayout();
    for (const UICommittedLayoutEntry& entry : layout.entries())
    {
        if (entry.node == textEdit)
        {
            placement = entry.contentPlacement;
            foundPlacement = true;
            break;
        }
    }
    if (textEdit.index() < textEditMultilineByNodeIndex.size() &&
        textEditMultilineByNodeIndex[textEdit.index()].enabled &&
        textEdit.index() < textEditVisualLayoutsByNodeIndex.size() &&
        textEditVisualLayoutsByNodeIndex[textEdit.index()].lineCount != 0)
    {
        const float relativeX = position.x - placement.origin.x;
        const float relativeY = position.y - placement.origin.y;
        const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
        const float fallbackAdvance = textState.style.logicalSize * textState.style.advanceScale;
        std::span<const UITextGlyphRaster> glyphs{};
        if (textRasterizer && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
            if (raster) { glyphs = raster->glyphs; }
        }
        return Detail::textEditHitFromVisualPosition(
            textViewFor(textEdit.index()), relativeX, relativeY,
            textEditScrollYByNodeIndex[textEdit.index()],
            textEditVisualLayoutsByNodeIndex[textEdit.index()],
            textEditVisualLinesByNodeIndex[textEdit.index()], fallbackAdvance, glyphs);
    }
    const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
    const u32 codepointCount = textState.metrics.codepointCount;
    if (!foundPlacement || codepointCount == 0)
    {
        return {};
    }
    const float relativeX = position.x - placement.origin.x;
    const float fallbackAdvance =
        textState.style.logicalSize * textState.style.advanceScale;

    if (textRasterizer && textFace.hasValue())
    {
        auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
        if (raster)
        {
            return {
                .codepoint = textEditCodepointFromHorizontalPosition(
                    textViewFor(textEdit.index()), relativeX, fallbackAdvance,
                    raster->glyphs),
            };
        }
    }
    return {
        .codepoint = textEditCodepointFromHorizontalPosition(
            textViewFor(textEdit.index()), relativeX, fallbackAdvance),
    };
}

[[nodiscard]] Core::Status UIContext::Impl::updateTextEditSelectionFromPointer(UINodeId textEdit, UILogicalPoint position,
                                                              bool extendSelection)
{
    if (!isLiveTextEdit(textEdit))
    {
        return Core::success();
    }
    Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
    if (state == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
    }
    UITextSelection next = state->selection;
    const Detail::UITextEditVisualHit hit =
        textEditHitFromPointer(textEdit, position);
    next.caretCodepoint = hit.codepoint;
    if (!extendSelection)
    {
        next.anchorCodepoint = next.caretCodepoint;
    }
    const bool affinityChanged =
        textEdit.index() < textEditCaretAffinityByNodeIndex.size() &&
        textEditCaretAffinityByNodeIndex[textEdit.index()] != hit.affinity;
    if (next == state->selection && !affinityChanged)
    {
        if (textEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[textEdit.index()].reset();
        }
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
    {
        return dirty;
    }
    if (Core::Status composition = clearImeComposition(); !composition)
    {
        return composition;
    }
    state->selection = next;
    // Pointer caret placement resets preferred-X.
    if (textEdit.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[textEdit.index()].reset();
    }
    if (textEdit.index() < textEditCaretAffinityByNodeIndex.size())
    {
        textEditCaretAffinityByNodeIndex[textEdit.index()] = hit.affinity;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue,
                                                     float maxValue, float step)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = resolveRangeInput(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    const NodeRecord* rangeRecord = nodes.tryGet(slider.storageId());
    if (rangeRecord != nullptr && rangeRecord->kind == BuiltinElementKind::Splitter &&
        (minValue != 0.0F || maxValue != 1.0F))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Splitter RangeInput is fixed to zero through one");
    }
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue) || (step < 0.0F) ||
        !std::isfinite(step))
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Slider range/step is invalid");
    }
    Detail::UIRangeInputState& state = *behaviorStateStorage.tryRangeInputState(slider.index());
    const float nextValue = quantizeSliderValue(state.value, minValue, maxValue, step);
    if (state.minValue == minValue && state.maxValue == maxValue && state.step == step && state.value == nextValue)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(slider); !dirty)
    {
        return dirty;
    }
    const float previousValue = state.value;
    state.minValue = minValue;
    state.maxValue = maxValue;
    state.step = step;
    state.value = nextValue;
    if (state.value != previousValue)
    {
        invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                            .sliderNode = slider,
                                                                            .value = state.value,
                                                                            .platformFrame = {},
                                                                            .sourceSequence = 0,
                                                                        });
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = resolveRangeInput(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    if (!std::isfinite(value))
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Slider value must be finite");
    }
    auto applied = applySliderValue(slider, value, {}, 0, false);
    if (!applied)
    {
        return Core::failure(applied.error());
    }
    return Core::success();
}

[[nodiscard]] Core::Result<float> UIContext::Impl::sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = const_cast<Impl*>(this)->resolveRangeInput(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    return behaviorStateStorage.tryRangeInputState(slider.index())->value;
}

[[nodiscard]] Core::Status UIContext::Impl::setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                     const UISliderPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = resolveSlider(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    if (!std::isfinite(paint.contentInset) || paint.contentInset < 0.0F ||
        !std::isfinite(paint.trackThickness) || paint.trackThickness <= 0.0F ||
        !std::isfinite(paint.thumbExtent) || paint.thumbExtent <= 0.0F ||
        paint.contentInset < paint.thumbExtent * 0.5F ||
        paint.trackThickness > paint.thumbExtent)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Slider paint metrics must be finite and non-negative");
    }
    UISliderPaint& state = sliderPaintsByNodeIndex[slider.index()];
    if (state == paint)
    {
        detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(slider); !dirty)
    {
        return dirty;
    }
    state = paint;
    detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UISliderPaint> UIContext::Impl::sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    return sliderPaintsByNodeIndex[slider.index()];
}

[[nodiscard]] Core::Status UIContext::Impl::setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                              UISliderChangeCallback&& callback)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = resolveSlider(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    if (!callback.hasValue())
    {
        return fail(UIErrorCode::InvalidButtonAction, "UI Slider change callback is empty");
    }

    auto registration = sliderChangeCallbackRegistry.stage(
        slider, std::move(callback), routeDispatchDepth != 0);
    if (!registration)
    {
        return Core::failure(registration.error());
    }

    const auto rollbackRegistration = [&](Core::Error error) {
        sliderChangeCallbackRegistry.rollback(*registration, routeDispatchDepth != 0);
        return Core::failure(std::move(error));
    };

    if (!contains(updaterRoot))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::RootRequired,
            "UI tree updater root was released while setting a Slider callback"));
    }
    auto liveSliderResult = resolveSlider(slider);
    if (!liveSliderResult)
    {
        return rollbackRegistration(liveSliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return rollbackRegistration(
            makeError(UIErrorCode::InvalidNode, "UI Slider left the updater root while setting its callback"));
    }
    if (!sliderChangeCallbackRegistry.canCommit(*registration))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::InvalidButtonAction,
            "UI Slider change callback changed during callback transfer"));
    }

    sliderChangeCallbackRegistry.commit(*registration, routeDispatchDepth != 0);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = resolveSlider(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    sliderChangeCallbackRegistry.clear(slider, routeDispatchDepth != 0);
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
    if (!sliderResult)
    {
        return Core::failure(sliderResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, slider))
    {
        return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
    }
    return armedSlider == slider;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveSplitView(UINodeId splitView)
{
    auto nodeResult = resolveNode(splitView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::SplitView ||
        !splitViewStorage.containsSplitView(splitView))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI SplitView API requires a SplitView node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveSplitter(UINodeId splitter)
{
    auto nodeResult = resolveNode(splitter);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Splitter ||
        !splitViewStorage.containsSplitter(splitter))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Splitter API requires a Splitter node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::validateSplitViewUpdaterRoot(
    UINodeId updaterRoot, UINodeId splitView) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto splitViewResult = const_cast<Impl*>(this)->resolveSplitView(splitView);
    if (!splitViewResult)
    {
        return Core::failure(splitViewResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, splitView))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI SplitView is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView, UINodeId primaryPane,
    UINodeId splitter, UINodeId secondaryPane)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return valid;
    }
    if (!primaryPane.hasValue() || !splitter.hasValue() || !secondaryPane.hasValue() ||
        primaryPane == splitter || primaryPane == secondaryPane || splitter == secondaryPane ||
        splitView == primaryPane || splitView == splitter || splitView == secondaryPane)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI SplitView parts must be four distinct live nodes");
    }
    auto primaryResult = resolveNode(primaryPane);
    auto splitterResult = resolveSplitter(splitter);
    auto secondaryResult = resolveNode(secondaryPane);
    if (!primaryResult)
    {
        return Core::failure(primaryResult.error());
    }
    if (!splitterResult)
    {
        return Core::failure(splitterResult.error());
    }
    if (!secondaryResult)
    {
        return Core::failure(secondaryResult.error());
    }
    const NodeRecord* splitViewRecord = nodes.tryGet(splitView.storageId());
    const NodeRecord* primaryRecord = *primaryResult;
    const NodeRecord* splitterRecord = *splitterResult;
    const NodeRecord* secondaryRecord = *secondaryResult;
    if (splitViewRecord == nullptr || primaryRecord->rootIndex != splitViewRecord->rootIndex ||
        splitterRecord->rootIndex != splitViewRecord->rootIndex ||
        secondaryRecord->rootIndex != splitViewRecord->rootIndex)
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI SplitView parts must be owned by the updater root");
    }
    if (primaryRecord->parentIndex != splitView.index() ||
        splitterRecord->parentIndex != splitView.index() ||
        secondaryRecord->parentIndex != splitView.index())
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI SplitView parts must be direct children of the SplitView");
    }
    if (primaryRecord->kind == BuiltinElementKind::Splitter ||
        secondaryRecord->kind == BuiltinElementKind::Splitter ||
        layoutStylesByIndex[primaryPane.index()].placement != UILayoutPlacement::Flow ||
        layoutStylesByIndex[splitter.index()].placement != UILayoutPlacement::Flow ||
        layoutStylesByIndex[secondaryPane.index()].placement != UILayoutPlacement::Flow)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI SplitView panes require ordinary Flow children and one Splitter");
    }
    usize directChildCount = 0;
    for (u32 childIndex = splitViewRecord->firstChildIndex;
         childIndex != InvalidNodeIndex;)
    {
        const NodeRecord* child = recordByIndex(childIndex);
        if (child == nullptr)
        {
            break;
        }
        ++directChildCount;
        childIndex = child->nextSiblingIndex;
    }
    if (directChildCount != 3)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI SplitView requires exactly three direct children");
    }
    for (const UINodeId part : {primaryPane, splitter, secondaryPane})
    {
        const UINodeId existingOwner = splitViewStorage.splitViewForPart(part);
        if (existingOwner.hasValue() && existingOwner != splitView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView part already belongs to another SplitView");
        }
    }

    const UISplitViewParts nextParts{primaryPane, splitter, secondaryPane};
    if (splitViewStorage.parts(splitView) == nextParts)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
    {
        return dirty;
    }
    splitViewStorage.linkValidated(splitView, nextParts);
    if (Detail::UIRangeInputState* range =
            behaviorStateStorage.tryRangeInputState(splitter.index());
        range != nullptr)
    {
        const SplitterState* splitterState = splitViewStorage.trySplitter(splitter);
        range->minValue = 0.0F;
        range->maxValue = 1.0F;
        range->step = splitterState != nullptr ? splitterState->config.keyboardStep : 0.02F;
        range->value = splitViewStorage.requestedFraction(splitView);
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearSplitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return valid;
    }
    const UISplitViewParts previous = splitViewStorage.parts(splitView);
    if (!previous.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
    {
        return dirty;
    }
    if (armedSlider == previous.splitter)
    {
        clearArmedSlider();
        if (capturedPointerNode == previous.splitter)
        {
            capturedPointerNode = {};
        }
    }
    static_cast<void>(splitViewStorage.unlinkSplitView(splitView));
    return Core::success();
}

[[nodiscard]] Core::Result<UISplitViewParts> UIContext::Impl::splitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return Core::failure(valid.error());
    }
    return splitViewStorage.parts(splitView);
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitViewFractionFromUpdater(
    UINodeId updaterRoot, UINodeId splitView, float fraction)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return valid;
    }
    if (!std::isfinite(fraction) || fraction < 0.0F || fraction > 1.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI SplitView fraction must be finite and within zero to one");
    }
    if (splitViewStorage.requestedFraction(splitView) == fraction)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
    {
        return dirty;
    }
    splitViewStorage.setRequestedFraction(splitView, fraction);
    const UISplitViewParts parts = splitViewStorage.parts(splitView);
    if (parts.hasValue())
    {
        if (Detail::UIRangeInputState* range =
                behaviorStateStorage.tryRangeInputState(parts.splitter.index());
            range != nullptr)
        {
            range->value = fraction;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<float> UIContext::Impl::splitViewFractionFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return Core::failure(valid.error());
    }
    return splitViewStorage.requestedFraction(splitView);
}

[[nodiscard]] Core::Result<UISplitViewMetrics> UIContext::Impl::splitViewMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
    {
        return Core::failure(valid.error());
    }
    return splitViewStorage.committedMetrics(splitView);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isSplitterDraggingFromUpdater(
    UINodeId updaterRoot, UINodeId splitter) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto splitterResult = const_cast<Impl*>(this)->resolveSplitter(splitter);
    if (!splitterResult)
    {
        return Core::failure(splitterResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, splitter))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Splitter is not owned by the updater root");
    }
    return armedSlider == splitter;
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitterPaintFromUpdater(
    UINodeId updaterRoot, UINodeId splitter, const UISplitterPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto splitterResult = resolveSplitter(splitter);
    if (!splitterResult)
    {
        return Core::failure(splitterResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, splitter))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Splitter is not owned by the updater root");
    }
    if (!std::isfinite(paint.lineThickness) || paint.lineThickness <= 0.0F ||
        !std::isfinite(paint.focusRingThickness) ||
        paint.focusRingThickness < paint.lineThickness)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Splitter paint thicknesses must be finite, positive, and ordered");
    }
    UISplitterPaint& state =
        splitViewStorage.splitterPaintByIndex(splitter.index());
    if (state == paint)
    {
        detachThemeBinding(splitter.index(), ThemeBindingSplitterPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(splitter); !dirty)
    {
        return dirty;
    }
    state = paint;
    detachThemeBinding(splitter.index(), ThemeBindingSplitterPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UISplitterPaint> UIContext::Impl::splitterPaintFromUpdater(
    UINodeId updaterRoot, UINodeId splitter) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto splitterResult = const_cast<Impl*>(this)->resolveSplitter(splitter);
    if (!splitterResult)
    {
        return Core::failure(splitterResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, splitter))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Splitter is not owned by the updater root");
    }
    return splitViewStorage.splitterPaintByIndex(splitter.index());
}

[[nodiscard]] UINodeId UIContext::Impl::rootForSplitView(UINodeId splitView) const noexcept
{
    const NodeRecord* record = nodes.tryGet(splitView.storageId());
    return record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitViewParts(
    UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
    UINodeId secondaryPane)
{
    return setSplitViewPartsFromUpdater(rootForSplitView(splitView), splitView,
                                        primaryPane, splitter, secondaryPane);
}

[[nodiscard]] Core::Status UIContext::Impl::clearSplitViewParts(UINodeId splitView)
{
    return clearSplitViewPartsFromUpdater(rootForSplitView(splitView), splitView);
}

[[nodiscard]] Core::Result<UISplitViewParts> UIContext::Impl::splitViewParts(UINodeId splitView) const
{
    return splitViewPartsFromUpdater(rootForSplitView(splitView), splitView);
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitViewFraction(UINodeId splitView, float fraction)
{
    return setSplitViewFractionFromUpdater(rootForSplitView(splitView), splitView, fraction);
}

[[nodiscard]] Core::Result<float> UIContext::Impl::splitViewFraction(UINodeId splitView) const
{
    return splitViewFractionFromUpdater(rootForSplitView(splitView), splitView);
}

[[nodiscard]] Core::Result<UISplitViewMetrics> UIContext::Impl::splitViewMetrics(UINodeId splitView) const
{
    return splitViewMetricsFromUpdater(rootForSplitView(splitView), splitView);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isSplitterDragging(UINodeId splitter) const
{
    const NodeRecord* record = nodes.tryGet(splitter.storageId());
    const UINodeId root = record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    return isSplitterDraggingFromUpdater(root, splitter);
}

[[nodiscard]] Core::Status UIContext::Impl::setSplitterPaint(
    UINodeId splitter, const UISplitterPaint& paint)
{
    const NodeRecord* record = nodes.tryGet(splitter.storageId());
    const UINodeId root =
        record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    return setSplitterPaintFromUpdater(root, splitter, paint);
}

[[nodiscard]] Core::Result<UISplitterPaint> UIContext::Impl::splitterPaint(
    UINodeId splitter) const
{
    const NodeRecord* record = nodes.tryGet(splitter.storageId());
    const UINodeId root =
        record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    return splitterPaintFromUpdater(root, splitter);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveProgressBar(UINodeId progressBar)
{
    auto nodeResult = resolveNode(progressBar);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::ProgressBar)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar API requires a ProgressBar node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolvePlainButton(UINodeId button)
{
    auto nodeResult = resolveNode(button);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isButtonChromeKind((*nodeResult)->kind))
    {
        return fail(UIErrorCode::InvalidButtonAction,
                    "UI Button paint API requires a Button, Dropdown, or DropdownItem node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                          float minValue, float maxValue)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto progressResult = resolveProgressBar(progressBar);
    if (!progressResult)
    {
        return Core::failure(progressResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, progressBar))
    {
        return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
    }
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ProgressBar range must be finite with max greater than min");
    }
    ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
    const float nextValue = std::clamp(state.value, minValue, maxValue);
    if (state.minValue == minValue && state.maxValue == maxValue && state.value == nextValue)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
    {
        return dirty;
    }
    state.minValue = minValue;
    state.maxValue = maxValue;
    state.value = nextValue;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto progressResult = resolveProgressBar(progressBar);
    if (!progressResult)
    {
        return Core::failure(progressResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, progressBar))
    {
        return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
    }
    if (!std::isfinite(value))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar value must be finite");
    }
    ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
    const float nextValue = std::clamp(value, state.minValue, state.maxValue);
    if (state.value == nextValue)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
    {
        return dirty;
    }
    state.value = nextValue;
    return Core::success();
}

[[nodiscard]] Core::Result<float> UIContext::Impl::progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
    if (!progressResult)
    {
        return Core::failure(progressResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, progressBar))
    {
        return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
    }
    return progressBarStatesByNodeIndex[progressBar.index()].value;
}

[[nodiscard]] Core::Status UIContext::Impl::setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                          const UIProgressBarPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto progressResult = resolveProgressBar(progressBar);
    if (!progressResult)
    {
        return Core::failure(progressResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, progressBar))
    {
        return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
    }
    ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
    if (state.paint == paint)
    {
        detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
    {
        return dirty;
    }
    state.paint = paint;
    detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIProgressBarPaint> UIContext::Impl::progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId progressBar) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
    if (!progressResult)
    {
        return Core::failure(progressResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, progressBar))
    {
        return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
    }
    return progressBarStatesByNodeIndex[progressBar.index()].paint;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveRadioButton(UINodeId radioButton)
{
    auto nodeResult = resolveNode(radioButton);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::RadioButton)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI RadioButton API requires a RadioButton node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::preflightDefaultActionActivationDirty(UINodeId target, bool pressedStateChanges) const
{
    const NodeRecord* targetRecord = nodes.tryGet(target.storageId());
    if (targetRecord == nullptr || !behaviorStateStorage.hasActivate(target.index()))
    {
        return fail(UIErrorCode::InvalidNode, "UI default-action target is stale");
    }

    usize requiredQueueEntries = 0;
    const auto countNode = [this, &requiredQueueEntries](UINodeId node) {
        if (node.hasValue() && contains(node) && !dirtyQueueStorage.isQueued(node.index()) &&
            !dirtyQueueStorage.isReserved(node.index()))
        {
            ++requiredQueueEntries;
        }
    };
    const bool targetStateChanges =
        pressedStateChanges || behaviorStateStorage.hasToggle(target.index());
    if (targetStateChanges)
    {
        countNode(target);
    }

    if (targetRecord->kind == BuiltinElementKind::RadioButton)
    {
        const NodeRecord* parent = recordByIndex(targetRecord->parentIndex);
        if (parent == nullptr)
        {
            return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
        }
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == target.index()) &&
                !(targetStateChanges && childIndex == target.index()) && !dirtyQueueStorage.isQueued(childIndex) &&
                !dirtyQueueStorage.isReserved(childIndex))
            {
                ++requiredQueueEntries;
            }
            childIndex = nextSiblingIndex;
        }
    }

    const usize occupiedQueueEntries = validDirtyQueueCount() + dirtyQueueStorage.reservationCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::applyRadioButtonSelection(UINodeId radioButton, bool selected)
{
    NodeRecord* radioRecord = nodes.tryGet(radioButton.storageId());
    if (radioRecord == nullptr || radioRecord->kind != BuiltinElementKind::RadioButton ||
        radioButton.index() >= radioButtonStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidNode, "UI RadioButton is stale");
    }
    if (!selected)
    {
        RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
        if (!state.selected)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(radioButton); !dirty)
        {
            return dirty;
        }
        state.selected = false;
        return Core::success();
    }

    const NodeRecord* parent = recordByIndex(radioRecord->parentIndex);
    if (parent == nullptr)
    {
        return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
    }
    usize requiredQueueEntries = 0;
    bool selectionChanged = false;
    for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
    {
        const NodeRecord* child = recordByIndex(childIndex);
        if (child == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
        }
        const u32 nextSiblingIndex = child->nextSiblingIndex;
        if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
        {
            const bool nextSelected = childIndex == radioButton.index();
            if (radioButtonStatesByNodeIndex[childIndex].selected != nextSelected)
            {
                selectionChanged = true;
                if (!dirtyQueueStorage.isQueued(childIndex) && !dirtyQueueStorage.isReserved(childIndex))
                {
                    ++requiredQueueEntries;
                }
            }
        }
        childIndex = nextSiblingIndex;
    }
    if (!selectionChanged)
    {
        return Core::success();
    }
    usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        compactDirtyQueue();
        requiredQueueEntries = 0;
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == radioButton.index()) &&
                !dirtyQueueStorage.isQueued(childIndex) && !dirtyQueueStorage.isReserved(childIndex))
            {
                ++requiredQueueEntries;
            }
            childIndex = nextSiblingIndex;
        }
        occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    }
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI RadioButton group selection exceeds dirty queue capacity");
    }
    for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
    {
        const NodeRecord* child = recordByIndex(childIndex);
        if (child == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
        }
        const u32 nextSiblingIndex = child->nextSiblingIndex;
        if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
        {
            RadioButtonState& state = radioButtonStatesByNodeIndex[childIndex];
            const bool nextSelected = childIndex == radioButton.index();
            if (state.selected != nextSelected)
            {
                if (!dirtyQueueStorage.isQueued(childIndex))
                {
                    dirtyQueueStorage.enqueue(idForIndex(childIndex));
                }
                dirtyQueueStorage.flags(childIndex) |= UIDirty::Paint | UIDirty::Semantics;
                state.selected = nextSelected;
            }
        }
        childIndex = nextSiblingIndex;
    }
    phaseDirty |= PhasePaint | PhaseSemantics;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                          const UIRadioButtonPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto radioResult = resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, radioButton))
    {
        return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
    }
    if (!std::isfinite(paint.indicatorExtent) || paint.indicatorExtent <= 0.0F ||
        !std::isfinite(paint.selectedIndicatorInset) || paint.selectedIndicatorInset < 0.0F ||
        paint.selectedIndicatorInset * 2.0F >= paint.indicatorExtent ||
        !std::isfinite(paint.labelGap) || paint.labelGap < 0.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI RadioButton paint metrics must be finite and non-negative");
    }
    RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
    if (state.paint == paint)
    {
        detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
        return Core::success();
    }
    const bool layoutChanged = state.paint.indicatorExtent != paint.indicatorExtent ||
                               state.paint.labelGap != paint.labelGap ||
                               state.paint.indicatorVisible != paint.indicatorVisible;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(radioButton) : markPaintDirty(radioButton);
    if (!dirty)
    {
        return dirty;
    }
    state.paint = paint;
    detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIRadioButtonPaint> UIContext::Impl::radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId radioButton) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, radioButton))
    {
        return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
    }
    return radioButtonStatesByNodeIndex[radioButton.index()].paint;
}

[[nodiscard]] Core::Status UIContext::Impl::setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                           UIButtonActionCallback&& callback)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    auto radioResult = resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    return setButtonActionFromUpdater(updaterRoot, radioButton, std::move(callback));
}

[[nodiscard]] Core::Status UIContext::Impl::clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    auto radioResult = resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    return clearButtonActionFromUpdater(updaterRoot, radioButton);
}

[[nodiscard]] Core::Status UIContext::Impl::setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                             bool selected)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto radioResult = resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, radioButton))
    {
        return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
    }
    return applyRadioButtonSelection(radioButton, selected);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, radioButton))
    {
        return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
    }
    return radioButtonStatesByNodeIndex[radioButton.index()].selected;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto radioResult = resolveRadioButton(radioButton);
    if (!radioResult)
    {
        return Core::failure(radioResult.error());
    }
    return isButtonPressedFromUpdater(updaterRoot, radioButton);
}

void UIContext::Impl::appendCommittedTree(u32 index, u32& ordinal, std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
{
    const u32 rootIndex = index;
    u32 currentIndex = rootIndex;
    while (currentIndex != InvalidNodeIndex)
    {
        const NodeRecord* record = recordByIndex(currentIndex);
        if (record == nullptr)
        {
            return;
        }

        const u32 currentOrdinal = ordinal++;
        output.push_back(UICommittedNodeEntry{
            .node = idForIndex(currentIndex),
            .parent = idForIndex(record->parentIndex),
            .depth = record->depth,
            .preorder = currentOrdinal,
            .paintOrdinal = currentOrdinal,
        });

        if (record->firstChildIndex != InvalidNodeIndex)
        {
            currentIndex = record->firstChildIndex;
            continue;
        }

        while (currentIndex != rootIndex)
        {
            record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }
            if (record->nextSiblingIndex != InvalidNodeIndex)
            {
                currentIndex = record->nextSiblingIndex;
                break;
            }
            currentIndex = record->parentIndex;
        }
        if (currentIndex == rootIndex)
        {
            currentIndex = InvalidNodeIndex;
        }
    }
}

} // namespace Tina::UI
