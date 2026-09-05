#include "detail/UIContextImpl.hpp"
#include "detail/UITextWrapping.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Status UIContext::Impl::buildTextEditVisualState(
    std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
{
    for (const UICommittedLayoutEntry& entry : layoutEntries)
    {
        const u32 index = entry.node.index();
        if (index >= textEditMultilineByNodeIndex.size() ||
            !textEditMultilineByNodeIndex[index].enabled ||
            index >= textStatesByIndex.size())
        {
            continue;
        }
        const UITextEditMultilineConfig config = textEditMultilineByNodeIndex[index];
        auto& lines = candidateTextEditVisualLinesByNodeIndex[index];
        lines.clear();
        const WidgetTextState& text = textStatesByIndex[index];
        const float lineHeight = text.style.logicalSize * text.style.lineHeightScale;
        const float fallback = text.style.logicalSize * text.style.advanceScale;
        if (!std::isfinite(lineHeight) || lineHeight <= 0.0F ||
            !std::isfinite(fallback) || fallback <= 0.0F)
        {
            return fail(UIErrorCode::InvalidText, "UI multiline TextEdit metrics are invalid");
        }
        const usize capacity = config.maximumVisualLines != 0
                                   ? config.maximumVisualLines
                                   : capacityConfig.textEditVisualLineCapacity;
        lines.resize(capacity);
        std::span<const UITextGlyphRaster> glyphs{};
        if (textRasterizer != nullptr && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(textFace, textViewFor(index), text.style);
            if (raster)
            {
                glyphs = raster->glyphs;
            }
        }
        Detail::UITextEditVisualLayout result{};
        if (!Detail::buildTextEditVisualLayout(
                textViewFor(index), entry.contentPlacement.contentBox.width,
                entry.contentPlacement.contentBox.height, lineHeight, fallback,
                config.wrapMode, glyphs, lines, result))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI multiline TextEdit visual-line capacity has been exhausted");
        }
        lines.resize(result.lineCount);
        candidateTextEditVisualLayoutsByNodeIndex[index] = result;
        float scroll = textEditScrollYByNodeIndex[index];
        if (!std::isfinite(scroll) || scroll < 0.0F)
        {
            scroll = 0.0F;
        }
        candidateTextEditScrollYByNodeIndex[index] =
            (std::min)(scroll, result.maximumScrollY);
    }
    return Core::success();
}

void UIContext::Impl::clearTextState(u32 index) noexcept
{
    if (index >= textStatesByIndex.size())
    {
        return;
    }
    WidgetTextState& state = textStatesByIndex[index];
    textStorage.release(state.allocation);
    state = {};
}

[[nodiscard]] std::string_view UIContext::Impl::semanticsNameViewFor(u32 index) const noexcept
{
    if (index >= semanticsStatesByNodeIndex.size())
    {
        return {};
    }
    const SemanticsState& state = semanticsStatesByNodeIndex[index];
    return textStorage.view(
        TextByteAllocation{
            .offset = state.textAllocation.offset,
            .capacity = state.nameLength,
        },
        state.nameLength);
}

[[nodiscard]] std::string_view UIContext::Impl::semanticsDescriptionViewFor(u32 index) const noexcept
{
    if (index >= semanticsStatesByNodeIndex.size())
    {
        return {};
    }
    const SemanticsState& state = semanticsStatesByNodeIndex[index];
    return textStorage.view(
        TextByteAllocation{
            .offset = state.textAllocation.offset + state.nameLength,
            .capacity = state.descriptionLength,
        },
        state.descriptionLength);
}

[[nodiscard]] std::string_view UIContext::Impl::semanticsNameSourceFor(u32 index) const noexcept
{
    if (index >= semanticsStatesByNodeIndex.size())
    {
        return {};
    }
    const SemanticsState& state = semanticsStatesByNodeIndex[index];
    if (state.hasExplicitName)
    {
        return semanticsNameViewFor(index);
    }
    return state.useContentAsName ? textViewFor(index) : std::string_view{};
}

[[nodiscard]] std::string_view UIContext::Impl::textViewFor(u32 index) const noexcept
{
    if (index >= textStatesByIndex.size())
    {
        return {};
    }
    const WidgetTextState& state = textStatesByIndex[index];
    if (!state.hasContent || state.length == 0 || state.allocation.capacity == 0)
    {
        return {};
    }
    return textStorage.view(state.allocation, state.length);
}

bool UIContext::Impl::acquireEventTextSnapshot(
    u32 index, Detail::UITextStorage::Allocation& allocation, u32& length) noexcept
{
    allocation = {};
    length = 0;
    const std::string_view live = textViewFor(index);
    if (live.empty())
    {
        // An empty view has nothing that can dangle, so no slot is needed.
        return true;
    }
    // No width check on the size: WidgetTextState::length is already u32 and
    // textViewFor derives the view from it, so the cast cannot narrow.
    auto reserved = textStorage.allocate(static_cast<u32>(live.size()));
    if (!reserved)
    {
        return false;
    }
    // Copy before any callback runs. The live allocation belongs to the widget and
    // is recycled by setText, so it cannot back a view that outlives this call.
    textStorage.write(*reserved, live);
    allocation = *reserved;
    length = static_cast<u32>(live.size());
    return true;
}

void UIContext::Impl::emitTextChanged(
    UINodeId textEdit, bool userInitiated,
    Platform::PlatformFrameId platformFrame, u64 sourceSequence) noexcept
{
    if (!contains(textEdit))
    {
        return;
    }
    const Detail::UITextInputState* inputState =
        behaviorStateStorage.tryTextInputState(textEdit.index());
    if (inputState == nullptr)
    {
        return;
    }
    const auto candidate = captureTextChangedCallback(textEdit);
    if (!candidate.hasValue())
    {
        return;
    }
    // The event text must be a private copy, not the widget's live storage.
    // Callbacks may reenter: calling setText, destroying the node, or triggering a
    // nested text event all recycle or overwrite that storage, and event.text would
    // then describe text the event was never raised for.
    Detail::UITextStorage::Allocation snapshot{};
    u32 snapshotLength = 0;
    if (!acquireEventTextSnapshot(textEdit.index(), snapshot, snapshotLength))
    {
        // Half an event is worse than none: a callback cannot tell a genuinely
        // empty field from one whose snapshot could not be allocated.
        ++textEventSnapshotCapacityFailureCount;
        return;
    }
    const UITextChangedEvent event{
        .textEdit = textEdit,
        .text = snapshotLength == 0U ? std::string_view{}
                                     : textStorage.view(snapshot, snapshotLength),
        .selection = inputState->selection,
        .userInitiated = userInitiated,
        .platformFrame = platformFrame,
        .sourceSequence = sourceSequence,
    };
    invokeTextChangedCallback(candidate, event);
    // Released after the callback returns, which is exactly the documented
    // lifetime of event.text.
    textStorage.release(snapshot);
}

void UIContext::Impl::emitTextSubmit(
    UINodeId textEdit, Platform::PlatformFrameId platformFrame,
    u64 sourceSequence) noexcept
{
    if (!contains(textEdit))
    {
        return;
    }
    const auto candidate = captureTextSubmitCallback(textEdit);
    if (!candidate.hasValue())
    {
        return;
    }
    // Same snapshot rule as the changed path; a submit callback that clears the
    // field is the common case, and it must not invalidate its own event text.
    Detail::UITextStorage::Allocation snapshot{};
    u32 snapshotLength = 0;
    if (!acquireEventTextSnapshot(textEdit.index(), snapshot, snapshotLength))
    {
        ++textEventSnapshotCapacityFailureCount;
        return;
    }
    const UITextSubmitEvent event{
        .textEdit = textEdit,
        .text = snapshotLength == 0U ? std::string_view{}
                                     : textStorage.view(snapshot, snapshotLength),
        .platformFrame = platformFrame,
        .sourceSequence = sourceSequence,
    };
    invokeTextSubmitCallback(candidate, event);
    textStorage.release(snapshot);
}

[[nodiscard]] std::string_view UIContext::Impl::presentationTextViewFor(u32 index) const noexcept
{
    const NodeRecord* record = recordByIndex(index);
    if (record != nullptr && record->kind == BuiltinElementKind::Dropdown && index < dropdownStatesByNodeIndex.size())
    {
        const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(index);
        const UINodeId selected = select != nullptr ? select->selectedOption : UINodeId{};
        if (contains(selected) && selected.index() < textStatesByIndex.size() &&
            textStatesByIndex[selected.index()].hasContent)
        {
            return textViewFor(selected.index());
        }
    }
    return textViewFor(index);
}

[[nodiscard]] const UITextMetrics* UIContext::Impl::presentationTextMetricsFor(u32 index) const noexcept
{
    if (index < layoutScratchByIndex.size() &&
        layoutScratchByIndex[index].hasResolvedTextMetrics)
    {
        return &layoutScratchByIndex[index].resolvedTextMetrics;
    }
    const NodeRecord* record = recordByIndex(index);
    if (record != nullptr && record->kind == BuiltinElementKind::Dropdown && index < dropdownStatesByNodeIndex.size())
    {
        const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(index);
        const UINodeId selected = select != nullptr ? select->selectedOption : UINodeId{};
        if (contains(selected) && selected.index() < textStatesByIndex.size() &&
            textStatesByIndex[selected.index()].hasContent)
        {
            return &textStatesByIndex[selected.index()].metrics;
        }
    }
    return index < textStatesByIndex.size() && textStatesByIndex[index].hasContent
               ? &textStatesByIndex[index].metrics
               : nullptr;
}

Core::Result<UITextMetrics> UIContext::Impl::measureWrappedWidgetText(
    u32 index, float maximumWidth,
    Detail::UITextIntrinsicWidths* intrinsicWidths)
{
    if (index >= textStatesByIndex.size())
    {
        return fail(UIErrorCode::InvalidText,
                    "UI wrapped text state is out of range");
    }
    const WidgetTextState& state = textStatesByIndex[index];
    if (!state.hasContent || state.wrapMode == UITextWrapMode::NoWrap)
    {
        if (intrinsicWidths != nullptr)
        {
            intrinsicWidths->minContent = state.metrics.measuredSize.width;
            intrinsicWidths->maxContent = state.metrics.measuredSize.width;
        }
        return state.metrics;
    }
    float ellipsisAdvance = state.style.logicalSize * state.style.advanceScale;
    if (state.lineClamp.enabled())
    {
        auto ellipsisMetrics = measureWidgetText(UITextEllipsisUtf8, state.style);
        if (ellipsisMetrics &&
            std::isfinite(ellipsisMetrics->measuredSize.width) &&
            ellipsisMetrics->measuredSize.width >= 0.0F)
        {
            ellipsisAdvance = ellipsisMetrics->measuredSize.width;
        }
    }
    if (textRasterizer != nullptr && textFace.hasValue())
    {
        auto raster = textRasterizer->raster(
            textFace, textViewFor(index), state.style);
        if (!raster)
        {
            return Core::failure(raster.error());
        }
        if (intrinsicWidths != nullptr)
        {
            *intrinsicWidths = Detail::measureTextIntrinsicWidths(
                textViewFor(index), state.style, state.wrapMode,
                raster->glyphs);
        }
        return Detail::measureWrappedText(
            textViewFor(index), state.style, maximumWidth, state.wrapMode,
            raster->glyphs, state.metrics.codepointCount, state.lineClamp,
            ellipsisAdvance);
    }
    if (intrinsicWidths != nullptr)
    {
        *intrinsicWidths = Detail::measureTextIntrinsicWidths(
            textViewFor(index), state.style, state.wrapMode, {});
    }
    return Detail::measureWrappedText(
        textViewFor(index), state.style, maximumWidth, state.wrapMode,
        {}, state.metrics.codepointCount, state.lineClamp, ellipsisAdvance);
}

[[nodiscard]] Core::Result<UITextMetrics> UIContext::Impl::measureWidgetText(std::string_view utf8, const UITextStyle& style)
{
    if (textRasterizer && textFace.hasValue())
    {
        return textRasterizer->measure(textFace, utf8, style);
    }
    // Fallback keeps measure available when a custom FreeType rasterizer is
    // injected before any face is opened.
    return measurePlaceholderText(utf8, style);
}

[[nodiscard]] Core::Status UIContext::Impl::setTextFromUpdater(
    UINodeId updaterRoot, UINodeId node, std::string_view utf8,
    bool userInitiated, Platform::PlatformFrameId platformFrame,
    u64 sourceSequence, bool emitChanged)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text requires an element with intrinsic text content");
    }
    const UITextEditMultilineConfig multiline =
        node.index() < textEditMultilineByNodeIndex.size()
            ? textEditMultilineByNodeIndex[node.index()] : UITextEditMultilineConfig{};
    if ((record->kind == BuiltinElementKind::RadioButton && containsLineBreak(utf8)) ||
        (utf8.find('\r') != std::string_view::npos) ||
        (record->kind == BuiltinElementKind::TextEdit && !multiline.enabled &&
         utf8.find('\n') != std::string_view::npos))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI RadioButton and single-line TextEdit accept one logical line without CR or LF");
    }
    if (record->kind == BuiltinElementKind::TextEdit && multiline.maximumBytes != 0 &&
        utf8.size() > multiline.maximumBytes)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI multiline TextEdit byte capacity has been exceeded");
    }
    if (record->kind == BuiltinElementKind::TextEdit && multiline.enabled &&
        multiline.maximumVisualLines != 0)
    {
        const usize hardLineCount = 1U + static_cast<usize>(std::count(utf8.begin(), utf8.end(), '\n'));
        if (hardLineCount > multiline.maximumVisualLines)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI multiline TextEdit visual-line capacity has been exceeded");
        }
    }
    if (utf8.size() > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI text payload is too large");
    }

    auto metrics = measureWidgetText(utf8, textStatesByIndex[node.index()].style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    const std::string_view current = textViewFor(node.index());
    const bool sameContent = state.hasContent == !utf8.empty() && current == utf8;
    const bool contentChanged = !sameContent;
    const bool clearActiveIme =
        record->kind == BuiltinElementKind::TextEdit && textInputFocus == node && imeComposition.active();
    if (sameContent && state.metrics == *metrics)
    {
        if (clearActiveIme)
        {
            return clearImeComposition();
        }
        return Core::success();
    }

    TextByteAllocation reservedAllocation{};
    bool reservedNewAllocation = false;
    if (!utf8.empty() && state.allocation.capacity < utf8.size())
    {
        auto allocation = textStorage.allocate(static_cast<u32>(utf8.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        reservedAllocation = *allocation;
        reservedNewAllocation = true;
    }

    if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
    {
        if (reservedNewAllocation)
        {
            textStorage.release(reservedAllocation);
        }
        return dirtyStatus;
    }
    if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
    {
        if (reservedNewAllocation)
        {
            textStorage.release(reservedAllocation);
        }
        return paintStatus;
    }

    if (utf8.empty())
    {
        textStorage.release(state.allocation);
        state.allocation = {};
        state.length = 0;
        state.metrics = {};
        state.hasContent = false;
        if (record->kind == BuiltinElementKind::TextEdit &&
            node.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[node.index()].reset();
        }
        if (record->kind == BuiltinElementKind::TextEdit &&
            node.index() < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[node.index()] =
                Detail::UITextEditCaretAffinity::Downstream;
        }
        if (Detail::UITextInputState* textInputState =
                behaviorStateStorage.tryTextInputState(node.index());
            textInputState != nullptr)
        {
            textInputState->selection = {};
            if (clearActiveIme)
            {
                resetImeCompositionState();
            }
        }
        if (emitChanged && contentChanged)
        {
            emitTextChanged(node, userInitiated, platformFrame, sourceSequence);
        }
        return Core::success();
    }

    if (reservedNewAllocation)
    {
        textStorage.release(state.allocation);
        state.allocation = reservedAllocation;
    }
    textStorage.write(state.allocation, utf8);
    state.length = static_cast<u32>(utf8.size());
    state.metrics = *metrics;
    state.hasContent = true;
    if (record->kind == BuiltinElementKind::TextEdit &&
        node.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[node.index()].reset();
    }
    if (record->kind == BuiltinElementKind::TextEdit &&
        node.index() < textEditCaretAffinityByNodeIndex.size())
    {
        textEditCaretAffinityByNodeIndex[node.index()] =
            Detail::UITextEditCaretAffinity::Downstream;
    }
    if (Detail::UITextInputState* textInputState =
            behaviorStateStorage.tryTextInputState(node.index());
        textInputState != nullptr)
    {
        UITextSelection& selection = textInputState->selection;
        selection.anchorCodepoint = Detail::nearestGraphemeBoundary(
            utf8, (std::min)(selection.anchorCodepoint, metrics->codepointCount));
        selection.caretCodepoint = Detail::nearestGraphemeBoundary(
            utf8, (std::min)(selection.caretCodepoint, metrics->codepointCount));
        if (clearActiveIme)
        {
            resetImeCompositionState();
        }
    }
    if (emitChanged && contentChanged)
    {
        emitTextChanged(node, userInitiated, platformFrame, sourceSequence);
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text style requires an element with intrinsic text content");
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    if (state.style == style)
    {
        detachThemeBinding(node.index(), ThemeBindingTextStyle);
        return Core::success();
    }

    UITextMetrics metrics{};
    if (state.hasContent)
    {
        auto measured = measureWidgetText(textViewFor(node.index()), style);
        if (!measured)
        {
            return Core::failure(measured.error());
        }
        metrics = *measured;
    }

    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::TextStyle);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    state.style = style;
    state.metrics = metrics;
    detachThemeBinding(node.index(), ThemeBindingTextStyle);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setContentAlignmentFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                           UIContentAlignment alignment)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI content alignment requires an element with intrinsic text");
    }
    if (!isValidContentAlignment(alignment))
    {
        return fail(UIErrorCode::InvalidLayout,
                    "UI intrinsic content alignment supports Start, Center, or End on each axis");
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    if (state.alignment == alignment)
    {
        return Core::success();
    }
    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::ContentAlignment);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    state.alignment = alignment;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTextOverflowFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                      UITextOverflow overflow)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text overflow requires an element with intrinsic text");
    }
    if (overflow != UITextOverflow::Clip && overflow != UITextOverflow::Ellipsis)
    {
        return fail(UIErrorCode::InvalidText, "UI text overflow must be Clip or Ellipsis");
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    if (overflow == UITextOverflow::Ellipsis &&
        state.wrapMode != UITextWrapMode::NoWrap)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI wrapped text cannot use single-line Ellipsis overflow");
    }
    if (state.overflow == overflow)
    {
        return Core::success();
    }
    // Paint only: intrinsic metrics keep describing the untruncated text, so
    // neither layout nor the accessibility name changes.
    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::TextOverflow);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    state.overflow = overflow;
    return Core::success();
}

[[nodiscard]] Core::Result<UITextOverflow> UIContext::Impl::textOverflowFromUpdater(
    UINodeId updaterRoot, UINodeId node)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text overflow requires an element with intrinsic text");
    }
    return textStatesByIndex[node.index()].overflow;
}

[[nodiscard]] Core::Status UIContext::Impl::setTextWrapModeFromUpdater(
    UINodeId updaterRoot, UINodeId node, UITextWrapMode wrapMode)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text wrapping requires an element with intrinsic text");
    }
    if (record->kind == BuiltinElementKind::TextEdit &&
        wrapMode != UITextWrapMode::NoWrap)
    {
        return fail(
            UIErrorCode::InvalidText,
            "UI TextEdit wrapping is controlled by UITextEditMultilineConfig");
    }
    if (wrapMode != UITextWrapMode::NoWrap && wrapMode != UITextWrapMode::Words)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text wrap mode must be NoWrap or Words");
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    if (wrapMode == UITextWrapMode::Words &&
        state.overflow == UITextOverflow::Ellipsis)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI wrapped text cannot use single-line Ellipsis overflow");
    }
    if (wrapMode != UITextWrapMode::Words && state.lineClamp.enabled())
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text line clamp requires Words wrapping");
    }
    if (state.wrapMode == wrapMode)
    {
        return Core::success();
    }
    if (Core::Status dirty =
            markStylePropertyDirty(node, UIStylePropertyKind::TextWrap);
        !dirty)
    {
        return dirty;
    }
    state.wrapMode = wrapMode;
    return Core::success();
}

[[nodiscard]] Core::Result<UITextWrapMode>
UIContext::Impl::textWrapModeFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text wrapping requires an element with intrinsic text");
    }
    return textStatesByIndex[node.index()].wrapMode;
}

[[nodiscard]] Core::Status UIContext::Impl::setTextLineClampFromUpdater(
    UINodeId updaterRoot, UINodeId node, UITextLineClamp lineClamp)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text line clamp requires an element with intrinsic text");
    }

    WidgetTextState& state = textStatesByIndex[node.index()];
    if (lineClamp.enabled() &&
        (record->kind == BuiltinElementKind::TextEdit ||
         state.wrapMode != UITextWrapMode::Words))
    {
        return fail(
            UIErrorCode::InvalidText,
            "UI text line clamp requires ordinary intrinsic text with Words wrapping");
    }
    if (state.lineClamp == lineClamp)
    {
        return Core::success();
    }
    if (Core::Status dirty =
            markStylePropertyDirty(node, UIStylePropertyKind::TextLineClamp);
        !dirty)
    {
        return dirty;
    }
    state.lineClamp = lineClamp;
    return Core::success();
}

[[nodiscard]] Core::Result<UITextLineClamp>
UIContext::Impl::textLineClampFromUpdater(
    UINodeId updaterRoot, UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text line clamp requires an element with intrinsic text");
    }
    return textStatesByIndex[node.index()].lineClamp;
}

[[nodiscard]] Core::Result<std::string_view> UIContext::Impl::textFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text requires an element with intrinsic text content");
    }
    return textViewFor(node.index());
}

[[nodiscard]] Core::Result<UITextStyle> UIContext::Impl::textStyleFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI text style requires an element with intrinsic text content");
    }
    return textStatesByIndex[node.index()].style;
}

[[nodiscard]] Core::Result<UIContentAlignment> UIContext::Impl::contentAlignmentFromUpdater(UINodeId updaterRoot,
                                                                            UINodeId node)
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
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr || !supportsWidgetText(record->kind))
    {
        return fail(UIErrorCode::InvalidText, "UI content alignment requires an element with intrinsic text");
    }
    return textStatesByIndex[node.index()].alignment;
}

[[nodiscard]] Core::Status UIContext::Impl::setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                       UITextSelection selection)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
    if (state == nullptr)
    {
        return fail(UIErrorCode::InvalidText, "UI selection requires a TextInput behavior");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
    }
    const u32 codepointCount = textStatesByIndex[textEdit.index()].metrics.codepointCount;
    if (selection.anchorCodepoint > codepointCount || selection.caretCodepoint > codepointCount)
    {
        return fail(UIErrorCode::InvalidText, "UI TextEdit selection exceeds the text length");
    }
    const std::string_view text = textViewFor(textEdit.index());
    if (!Detail::isGraphemeBoundary(text, selection.anchorCodepoint) ||
        !Detail::isGraphemeBoundary(text, selection.caretCodepoint))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI TextEdit selection must align to grapheme boundaries");
    }
    const bool affinityChanged =
        textEdit.index() < textEditCaretAffinityByNodeIndex.size() &&
        textEditCaretAffinityByNodeIndex[textEdit.index()] !=
            Detail::UITextEditCaretAffinity::Downstream;
    if (state->selection == selection && !affinityChanged)
    {
        if (textEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[textEdit.index()].reset();
        }
        return Core::success();
    }
    if (Core::Status dirtyStatus = markPaintDirty(textEdit); !dirtyStatus)
    {
        return dirtyStatus;
    }
    if (textInputFocus == textEdit)
    {
        static_cast<void>(clearImeComposition());
    }
    state->selection = selection;
    if (textEdit.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[textEdit.index()].reset();
    }
    if (textEdit.index() < textEditCaretAffinityByNodeIndex.size())
    {
        textEditCaretAffinityByNodeIndex[textEdit.index()] =
            Detail::UITextEditCaretAffinity::Downstream;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UITextSelection> UIContext::Impl::textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
    if (state == nullptr)
    {
        return fail(UIErrorCode::InvalidText, "UI selection requires a TextInput behavior");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
    }
    return state->selection;
}

[[nodiscard]] Core::Status UIContext::Impl::setTextEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                       const UITextEditPaint& paint)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TextEdit paint requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
    }
    UITextEditPaint& currentPaint = textEditPaintsByNodeIndex[textEdit.index()];
    if (currentPaint == paint)
    {
        if ((styleOverridesByNodeIndex[textEdit.index()] &
             static_cast<u16>(UIStyleOverride::TextEditPaint)) != 0)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
        {
            return dirty;
        }
        detachThemeBinding(textEdit.index(), ThemeBindingTextEditPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
    {
        return dirty;
    }
    currentPaint = paint;
    detachThemeBinding(textEdit.index(), ThemeBindingTextEditPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UITextEditPaint> UIContext::Impl::textEditPaintFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId textEdit) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TextEdit paint requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
    }
    return textEditPaintsByNodeIndex[textEdit.index()];
}

[[nodiscard]] Core::Status UIContext::Impl::setTextChangedCallbackFromUpdater(
    UINodeId updaterRoot, UINodeId textEdit, UITextChangedCallback&& callback)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit ||
        behaviorStateStorage.tryTextInputState(textEdit.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text changed callback requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI TextEdit is not owned by the updater root");
    }
    auto registration = textChangedCallbackRegistry.stage(
        textEdit, std::move(callback), routeDispatchDepth != 0);
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    const auto rollbackRegistration = [&](Core::Error error) {
        textChangedCallbackRegistry.rollback(*registration, routeDispatchDepth != 0);
        return Core::failure(std::move(error));
    };
    if (!contains(textEdit) || !isNodeWithinRoot(updaterRoot, textEdit))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::InvalidNode,
            "UI TextEdit left the updater root while setting its callback"));
    }
    if (!textChangedCallbackRegistry.canCommit(*registration))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::InvalidText,
            "UI TextEdit changed callback changed during callback transfer"));
    }
    return textChangedCallbackRegistry.commit(*registration, routeDispatchDepth != 0);
}

[[nodiscard]] Core::Status UIContext::Impl::clearTextChangedCallbackFromUpdater(
    UINodeId updaterRoot, UINodeId textEdit)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit ||
        behaviorStateStorage.tryTextInputState(textEdit.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text changed callback requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI TextEdit is not owned by the updater root");
    }
    textChangedCallbackRegistry.clear(textEdit, routeDispatchDepth != 0);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTextSubmitCallbackFromUpdater(
    UINodeId updaterRoot, UINodeId textEdit, UITextSubmitCallback&& callback)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit ||
        behaviorStateStorage.tryTextInputState(textEdit.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text submit callback requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI TextEdit is not owned by the updater root");
    }
    auto registration = textSubmitCallbackRegistry.stage(
        textEdit, std::move(callback), routeDispatchDepth != 0);
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    const auto rollbackRegistration = [&](Core::Error error) {
        textSubmitCallbackRegistry.rollback(*registration, routeDispatchDepth != 0);
        return Core::failure(std::move(error));
    };
    if (!contains(textEdit) || !isNodeWithinRoot(updaterRoot, textEdit))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::InvalidNode,
            "UI TextEdit left the updater root while setting its callback"));
    }
    if (!textSubmitCallbackRegistry.canCommit(*registration))
    {
        return rollbackRegistration(makeError(
            UIErrorCode::InvalidText,
            "UI TextEdit submit callback changed during callback transfer"));
    }
    return textSubmitCallbackRegistry.commit(*registration, routeDispatchDepth != 0);
}

[[nodiscard]] Core::Status UIContext::Impl::clearTextSubmitCallbackFromUpdater(
    UINodeId updaterRoot, UINodeId textEdit)
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
    auto nodeResult = resolveNode(textEdit);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    const NodeRecord* record = nodes.tryGet(textEdit.storageId());
    if (record == nullptr || record->kind != BuiltinElementKind::TextEdit ||
        behaviorStateStorage.tryTextInputState(textEdit.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidText,
                    "UI text submit callback requires a TextEdit node");
    }
    if (!isNodeWithinRoot(updaterRoot, textEdit))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI TextEdit is not owned by the updater root");
    }
    textSubmitCallbackRegistry.clear(textEdit, routeDispatchDepth != 0);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!textRasterizer)
    {
        return fail(UIErrorCode::InvalidFont, "UI context has no text rasterizer");
    }

    auto newFace = textRasterizer->openFace(fontBytes, faceIndex);
    if (!newFace)
    {
        return Core::failure(newFace.error());
    }

    if (textFace.hasValue())
    {
        static_cast<void>(textRasterizer->closeFace(textFace));
        textFace = {};
    }
    textFace = *newFace;
    if (glyphAtlas)
    {
        glyphAtlas->clear();
    }

    // Remeasure retained text and dirty layout/paint for all text nodes.
    for (u32 index = 0; index < static_cast<u32>(textStatesByIndex.size()); ++index)
    {
        WidgetTextState& state = textStatesByIndex[index];
        if (!state.hasContent)
        {
            continue;
        }
        const UINodeId node = idForIndex(index);
        if (!node.hasValue() || !contains(node))
        {
            continue;
        }
        auto metrics = measureWidgetText(textViewFor(index), state.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }
        state.metrics = *metrics;
        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            return paintStatus;
        }
    }
    return Core::success();
}

[[nodiscard]] UINodeId UIContext::Impl::defaultActionFocus() const noexcept
{
    if (!isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
    {
        return {};
    }
    const NodeRecord* record = nodes.tryGet(defaultActionFocusButton.storageId());
    if (record != nullptr && record->kind == BuiltinElementKind::TextEdit && textInputFocus != defaultActionFocusButton)
    {
        return {};
    }
    return defaultActionFocusButton;
}

[[nodiscard]] UINodeId UIContext::Impl::imeFocus() const noexcept
{
    if (!isCommittedTextEditFocusCandidate(textInputFocus) || defaultActionFocusButton != textInputFocus)
    {
        return {};
    }
    return textInputFocus;
}

[[nodiscard]] bool UIContext::Impl::imeCompositionActive() const noexcept
{
    return imeComposition.active() && isCommittedTextEditFocusCandidate(textInputFocus) &&
           defaultActionFocusButton == textInputFocus;
}

[[nodiscard]] std::string_view UIContext::Impl::imePreeditUtf8() const noexcept
{
    return imeComposition.preeditUtf8();
}

[[nodiscard]] u32 UIContext::Impl::imePreeditCursorCodepoint() const noexcept
{
    return imeComposition.cursorCodepoint();
}

[[nodiscard]] Core::Result<UITextInputRouteResult>
UIContext::Impl::routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                     std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!window.hasValue() || window != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI text composition belongs to another owner window");
    }
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI text composition requires a platform frame and sequence");
    }

    using Stage = Platform::TextCompositionStage;
    const bool knownStage =
        stage == Stage::Started || stage == Stage::Updated || stage == Stage::Ended || stage == Stage::Cancelled;
    if (!knownStage)
    {
        return fail(UIErrorCode::InvalidText, "UI text composition stage is not recognized");
    }
    if (const UINodeId activeMenuNode = menuStorage.rootMenu(); activeMenuNode.hasValue())
    {
        if (Core::Status closed = setMenuOpenState(activeMenuNode, false); !closed)
        {
            return Core::failure(closed.error());
        }
    }
    if (!isCommittedTextEditFocusCandidate(textInputFocus))
    {
        hardDismissAllTooltipsNoFail(true);
        clearImeFocus();
        return UITextInputRouteResult{};
    }

    if (stage == Stage::Cancelled || stage == Stage::Ended)
    {
        // clearImeComposition marks paint dirty when preedit was active.
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        hardDismissAllTooltipsNoFail(true);
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }
    if (!Core::isStrictUtf8WithoutNul(preeditUtf8))
    {
        return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
    }
    if (containsLineBreak(preeditUtf8))
    {
        return fail(UIErrorCode::InvalidText, "UI TextEdit preedit accepts one logical line without CR or LF");
    }
    if (Core::Status capacityStatus = Detail::UIImeCompositionState::validateCapacity(preeditUtf8);
        !capacityStatus)
    {
        return Core::failure(capacityStatus.error());
    }
    const auto codepoints = Core::countStrictUtf8CodepointsWithoutNul(preeditUtf8);
    if (!codepoints.has_value())
    {
        return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
    }
    if (Core::Status paintStatus = markPaintDirty(textInputFocus); !paintStatus)
    {
        return Core::failure(paintStatus.error());
    }
    imeComposition.assign(preeditUtf8, cursorCodepoint, *codepoints);
    hardDismissAllTooltipsNoFail(true);
    return UITextInputRouteResult{.consumed = true, .applied = true};
}

[[nodiscard]] Core::Result<UITextInputRouteResult> UIContext::Impl::routeTextInput(Platform::WindowId window,
                                                                  Platform::PlatformFrameId platformFrame,
                                                                  u64 sourceSequence,
                                                                  std::string_view committedUtf8)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!window.hasValue() || window != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI text input belongs to another owner window");
    }
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI text input requires a platform frame and sequence");
    }
    const bool validCommittedUtf8 = Core::isStrictUtf8WithoutNul(committedUtf8);
    if (validCommittedUtf8)
    {
        if (const UINodeId activeMenuNode = menuStorage.rootMenu(); activeMenuNode.hasValue())
        {
            if (Core::Status closed = setMenuOpenState(activeMenuNode, false); !closed)
            {
                return Core::failure(closed.error());
            }
        }
    }
    if (!isCommittedTextEditFocusCandidate(textInputFocus))
    {
        if (validCommittedUtf8)
        {
            hardDismissAllTooltipsNoFail(true);
        }
        clearImeFocus();
        return UITextInputRouteResult{};
    }
    if (!validCommittedUtf8)
    {
        return fail(UIErrorCode::InvalidText, "UI text input must be strict UTF-8 without embedded NUL");
    }
    const UITextEditMultilineConfig multiline =
        textInputFocus.index() < textEditMultilineByNodeIndex.size()
            ? textEditMultilineByNodeIndex[textInputFocus.index()] : UITextEditMultilineConfig{};
    if (committedUtf8.find('\r') != std::string_view::npos ||
        (!multiline.enabled && committedUtf8.find('\n') != std::string_view::npos))
    {
        hardDismissAllTooltipsNoFail(true);
        return UITextInputRouteResult{.consumed = true, .applied = false};
    }
    if (committedUtf8.empty())
    {
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        hardDismissAllTooltipsNoFail(true);
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    const UINodeId focusedTextEdit = textInputFocus;
    const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
    if (record == nullptr)
    {
        clearImeFocus();
        return UITextInputRouteResult{};
    }
    const UINodeId rootNode = idForIndex(record->rootIndex);
    if (!rootNode.hasValue())
    {
        clearImeFocus();
        return UITextInputRouteResult{};
    }

    const std::string_view current = textViewFor(focusedTextEdit.index());
    Detail::UITextInputState* textInputState =
        behaviorStateStorage.tryTextInputState(focusedTextEdit.index());
    if (textInputState == nullptr)
    {
        clearImeFocus();
        return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
    }
    const UITextSelection selection = textInputState->selection;
    const u32 selectionBegin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
    const u32 selectionEnd = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
    const usize selectionBeginByte = utf8ByteOffsetForCodepoint(current, selectionBegin);
    const usize selectionEndByte = utf8ByteOffsetForCodepoint(current, selectionEnd);
    const usize retainedBytes = current.size() - (selectionEndByte - selectionBeginByte);
    if (retainedBytes > (std::numeric_limits<usize>::max)() - committedUtf8.size())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI text input would overflow the text byte capacity");
    }
    std::string combined;
    try
    {
        // One-shot commit allocation; not a per-frame hot path.
        combined.reserve(retainedBytes + committedUtf8.size());
        combined.append(current.substr(0, selectionBeginByte));
        combined.append(committedUtf8);
        combined.append(current.substr(selectionEndByte));
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI text input scratch allocation failed");
    }
    if (Core::Status status = setTextFromUpdater(
            rootNode, focusedTextEdit, combined, true, platformFrame,
            sourceSequence, false);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status dirty = markPaintDirty(focusedTextEdit); !dirty)
    {
        return Core::failure(dirty.error());
    }
    const auto insertedCodepoints = Core::countStrictUtf8CodepointsWithoutNul(committedUtf8);
    const u32 insertedEnd = selectionBegin + insertedCodepoints.value_or(0U);
    const u32 nextCaret =
        Detail::graphemeBoundaryAtOrAfter(combined, insertedEnd);
    textInputState->selection = {
        .anchorCodepoint = nextCaret,
        .caretCodepoint = nextCaret,
    };
    // Reset preferred-X after text insertion.
    if (multiline.enabled && focusedTextEdit.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[focusedTextEdit.index()].reset();
    }
    if (focusedTextEdit.index() < textEditCaretAffinityByNodeIndex.size())
    {
        textEditCaretAffinityByNodeIndex[focusedTextEdit.index()] =
            Detail::UITextEditCaretAffinity::Downstream;
    }
    if (Core::Status status = clearImeComposition(); !status)
    {
        return Core::failure(status.error());
    }
    emitTextChanged(focusedTextEdit, true, platformFrame, sourceSequence);
    hardDismissAllTooltipsNoFail(true);
    return UITextInputRouteResult{.consumed = true, .applied = true};
}

[[nodiscard]] Core::Result<UITextInputRouteResult>
UIContext::Impl::routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                     UITextEditCommand command, bool extendSelection)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!window.hasValue() || window != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI TextEdit command belongs to another owner window");
    }
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI TextEdit command requires a platform frame and sequence");
    }
    if (!isCommittedTextEditFocusCandidate(textInputFocus))
    {
        clearImeFocus();
        return UITextInputRouteResult{};
    }

    const UINodeId focusedTextEdit = textInputFocus;
    const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
    if (record == nullptr)
    {
        clearImeFocus();
        return UITextInputRouteResult{};
    }
    const UINodeId rootNode = idForIndex(record->rootIndex);
    if (!rootNode.hasValue())
    {
        clearImeFocus();
        return UITextInputRouteResult{};
    }

    Detail::UITextInputState* editState =
        behaviorStateStorage.tryTextInputState(focusedTextEdit.index());
    if (editState == nullptr)
    {
        clearImeFocus();
        return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
    }
    const std::string_view current =
        textViewFor(focusedTextEdit.index());
    const UITextSelection currentSelection = editState->selection;
    const u32 idx = focusedTextEdit.index();
    const auto multilineConfig = idx < textEditMultilineByNodeIndex.size()
                                     ? textEditMultilineByNodeIndex[idx]
                                     : UITextEditMultilineConfig{};
    if (command == UITextEditCommand::Submit)
    {
        if (multilineConfig.enabled)
        {
            // Multiline Enter is ordinary committed text. Reuse the same
            // insertion path so byte/line limits, selection replacement and
            // changed-event ordering remain identical to IME commits.
            return routeTextInput(window, platformFrame, sourceSequence, "\n");
        }
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        emitTextSubmit(focusedTextEdit, platformFrame, sourceSequence);
        hardDismissAllTooltipsNoFail(true);
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }
    const bool isVerticalCommand =
        command == UITextEditCommand::MoveUp || command == UITextEditCommand::MoveDown;
    const Detail::UITextEditCaretAffinity currentCaretAffinity =
        idx < textEditCaretAffinityByNodeIndex.size()
            ? textEditCaretAffinityByNodeIndex[idx]
            : Detail::UITextEditCaretAffinity::Downstream;
    std::optional<UITextEditCommandPlan> plan;
    if (multilineConfig.enabled && idx < textEditVisualLayoutsByNodeIndex.size() &&
        textEditVisualLayoutsByNodeIndex[idx].lineCount != 0 &&
        (command == UITextEditCommand::MoveLeft || command == UITextEditCommand::MoveRight ||
         command == UITextEditCommand::MoveUp || command == UITextEditCommand::MoveDown ||
         command == UITextEditCommand::MoveHome || command == UITextEditCommand::MoveEnd))
    {
        const float fallback = textStatesByIndex[idx].style.logicalSize *
                               textStatesByIndex[idx].style.advanceScale;
        const std::optional<float> preferredX =
            isVerticalCommand && idx < textEditPreferredXByNodeIndex.size()
                ? textEditPreferredXByNodeIndex[idx]
                : std::nullopt;
        std::span<const UITextGlyphRaster> glyphs{};
        if (isVerticalCommand && textRasterizer != nullptr && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(
                textFace, current, textStatesByIndex[idx].style);
            if (raster)
            {
                glyphs = raster->glyphs;
            }
        }
        plan = Detail::planTextEditVisualCommand(
            current, editState->selection, command, extendSelection,
            textEditVisualLinesByNodeIndex[idx], currentCaretAffinity,
            preferredX, fallback, glyphs);
    }
    else
    {
        plan = planTextEditCommand(
            current, currentSelection, command, extendSelection);
    }
    if (!plan.has_value())
    {
        return fail(UIErrorCode::InvalidText, "UI TextEdit command is not recognized");
    }
    const std::optional<float> nextPreferredX =
        isVerticalCommand ? plan->updatedPreferredX : std::nullopt;
    const bool caretAffinityChanged =
        plan->nextCaretAffinity != currentCaretAffinity;
    const auto commitNavigationState = [&]() noexcept {
        if (idx < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[idx] = nextPreferredX;
        }
        if (idx < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[idx] = plan->nextCaretAffinity;
        }
    };

    if (!plan->deletesText)
    {
        if (plan->nextSelection == currentSelection && !caretAffinityChanged)
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            commitNavigationState();
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        if (Core::Status paintStatus = markPaintDirty(focusedTextEdit); !paintStatus)
        {
            return Core::failure(paintStatus.error());
        }
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        editState->selection = plan->nextSelection;
        commitNavigationState();
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    if (plan->deleteBeginCodepoint == plan->deleteEndCodepoint)
    {
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        commitNavigationState();
        return UITextInputRouteResult{.consumed = true, .applied = false};
    }
    const usize deleteBeginByte =
        utf8ByteOffsetForCodepoint(current, plan->deleteBeginCodepoint);
    const usize deleteEndByte =
        utf8ByteOffsetForCodepoint(current, plan->deleteEndCodepoint);
    std::string combined;
    try
    {
        combined.reserve(current.size() - (deleteEndByte - deleteBeginByte));
        combined.append(current.substr(0, deleteBeginByte));
        combined.append(current.substr(deleteEndByte));
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI TextEdit command scratch allocation failed");
    }
    if (Core::Status status = setTextFromUpdater(
            rootNode, focusedTextEdit, combined, true, platformFrame,
            sourceSequence, false);
        !status)
    {
        return Core::failure(status.error());
    }
    const u32 nextCaret = Detail::graphemeBoundaryAtOrAfter(
        combined, plan->deleteBeginCodepoint);
    editState->selection = {
        .anchorCodepoint = nextCaret,
        .caretCodepoint = nextCaret,
    };
    // Reset preferred-X after text deletion.
    if (multilineConfig.enabled && focusedTextEdit.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[focusedTextEdit.index()].reset();
    }
    if (Core::Status status = clearImeComposition(); !status)
    {
        return Core::failure(status.error());
    }
    commitNavigationState();
    emitTextChanged(focusedTextEdit, true, platformFrame, sourceSequence);
    return UITextInputRouteResult{.consumed = true, .applied = true};
}

} // namespace Tina::UI
