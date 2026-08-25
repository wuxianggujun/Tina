#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

void UIContext::Impl::buildCommittedStructure(std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
{
    output.clear();
    u32 ordinal = 0;
    u32 rootIndex = firstRootIndex;
    while (rootIndex != InvalidNodeIndex)
    {
        const NodeRecord* root = recordByIndex(rootIndex);
        const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
        appendCommittedTree(rootIndex, ordinal, output);
        rootIndex = nextRootIndex;
    }
}

void UIContext::Impl::appendLayoutOrderTree(u32 index, std::pmr::vector<u32>& output) const noexcept
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

        output.push_back(currentIndex);

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

void UIContext::Impl::buildLayoutOrder(std::pmr::vector<u32>& output) const noexcept
{
    output.clear();
    u32 rootIndex = firstRootIndex;
    while (rootIndex != InvalidNodeIndex)
    {
        const NodeRecord* root = recordByIndex(rootIndex);
        const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
        appendLayoutOrderTree(rootIndex, output);
        rootIndex = nextRootIndex;
    }
}

void UIContext::Impl::markLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
{
    const u8 completion = layoutSubtreeCompletionMask(work);
    u32 currentIndex = rootIndex;
    while (currentIndex != InvalidNodeIndex)
    {
        const NodeRecord* record = recordByIndex(currentIndex);
        if (record == nullptr)
        {
            return;
        }
        layoutWorkByIndex[currentIndex] |= work | completion;

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

void UIContext::Impl::ensureLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
{
    if (rootIndex >= layoutWorkByIndex.size())
    {
        return;
    }
    const u8 requiredCompletion = layoutSubtreeCompletionMask(work);
    if ((layoutWorkByIndex[rootIndex] & requiredCompletion) == requiredCompletion)
    {
        return;
    }
    markLayoutSubtreeWork(rootIndex, work);
}

void UIContext::Impl::markLayoutAncestorsWork(u32 nodeIndex, u8 work) noexcept
{
    u32 currentIndex = nodeIndex;
    while (currentIndex != InvalidNodeIndex)
    {
        layoutWorkByIndex[currentIndex] |= work;
        const NodeRecord* record = recordByIndex(currentIndex);
        if (record == nullptr)
        {
            return;
        }
        currentIndex = record->parentIndex;
    }
}

void UIContext::Impl::initializeLayoutWork(const std::pmr::vector<u32>& order, bool allowReuse) noexcept
{
    for (const u32 index : order)
    {
        layoutWorkByIndex[index] = 0;
    }

    if (!allowReuse)
    {
        for (const u32 index : order)
        {
            layoutWorkByIndex[index] = LayoutWorkMeasure | LayoutWorkArrange;
        }
        return;
    }

    for (const u32 index : order)
    {
        const UIDirty dirty = dirtyQueueStorage.flags(index);
        if (hasDirty(dirty, UIDirty::Measure))
        {
            layoutWorkByIndex[index] |= LayoutWorkMeasure | LayoutWorkArrange;
        } else if (hasDirty(dirty, UIDirty::Arrange))
        {
            layoutWorkByIndex[index] |= LayoutWorkArrange;
        }
        if (hasDirty(dirty, UIDirty::Style))
        {
            // A direct style change can alter the containing basis or
            // effective visibility of any descendant.
            ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
            markLayoutAncestorsWork(index, LayoutWorkArrange);
        }
    }
}

[[nodiscard]] bool UIContext::Impl::isActiveFlowScreenIndex(u32 index) const noexcept
{
    if (index >= flowStatesByNodeIndex.size())
    {
        return false;
    }
    const UIFlowNodeState& screenState = flowStatesByNodeIndex[index];
    if (screenState.kind != UIFlowNodeKind::Screen || !screenState.stacked ||
        !contains(screenState.layer))
    {
        return false;
    }
    const u32 layerIndex = screenState.layer.index();
    return layerIndex < flowStatesByNodeIndex.size() &&
           flowStatesByNodeIndex[layerIndex].kind == UIFlowNodeKind::Layer &&
           flowStatesByNodeIndex[layerIndex].top == idForIndex(index);
}

void UIContext::Impl::prepareLayoutState(UILogicalSize viewportSize, const std::pmr::vector<u32>& order, bool allowReuse) noexcept
{
    initializeLayoutWork(order, allowReuse);
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            continue;
        }
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const LayoutPreparedInputs previous = scratch.preparedInputs;
        if (!allowReuse)
        {
            scratch = {};
        }
        if (record->parentIndex == InvalidNodeIndex)
        {
            scratch.inPopupSubtree = record->kind == BuiltinElementKind::Popup ||
                                     record->kind == BuiltinElementKind::Menu;
            scratch.inTooltipSubtree = record->kind == BuiltinElementKind::Tooltip;
            scratch.parentContentWidthDefinite = true;
            scratch.parentContentHeightDefinite = true;
            scratch.parentContentWidth = viewportSize.width;
            scratch.parentContentHeight = viewportSize.height;
        } else
        {
            const LayoutScratchState& parentScratch =
                layoutScratchByIndex[record->parentIndex];
            scratch.inPopupSubtree =
                record->kind == BuiltinElementKind::Popup ||
                record->kind == BuiltinElementKind::Menu ||
                parentScratch.inPopupSubtree;
            scratch.inTooltipSubtree =
                record->kind == BuiltinElementKind::Tooltip ||
                parentScratch.inTooltipSubtree;
            scratch.parentContentWidthDefinite =
                parentScratch.contentWidthDefinite;
            scratch.parentContentHeightDefinite =
                parentScratch.contentHeightDefinite;
            scratch.parentContentWidth = parentScratch.contentWidth;
            scratch.parentContentHeight = parentScratch.contentHeight;
        }
        const UILayoutStyle authoredStyle = presentationLayoutStyle(index);
        scratch.resolvedStyle = scratch.parentContentWidthDefinite
                                    ? resolveResponsiveLayoutStyle(
                                          authoredStyle,
                                          scratch.parentContentWidth)
                                    : resolveResponsiveLayoutStyle(
                                          authoredStyle, -1.0F);
        const UILayoutStyle& style = scratch.resolvedStyle;
        UIVisibility ownVisibility = style.visibility;
        if (record->kind == BuiltinElementKind::Popup && index < popupStatesByNodeIndex.size() &&
            !popupStatesByNodeIndex[index].open)
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        const UINodeId node = idForIndex(index);
        const TooltipState* tooltip =
            record->kind == BuiltinElementKind::Tooltip
                ? tooltipStorage.tryState(node)
                : nullptr;
        if (tooltip != nullptr && !tooltip->open)
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind == BuiltinElementKind::Modal &&
            dialogStorage.containsDialog(node) && !dialogStorage.isOpen(node))
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind == BuiltinElementKind::Menu &&
            menuStorage.containsMenu(node))
        {
            if (!menuStorage.isOpen(node))
            {
                ownVisibility = UIVisibility::Collapsed;
            }
        }
        if (index < flowStatesByNodeIndex.size() &&
            flowStatesByNodeIndex[index].kind == UIFlowNodeKind::Screen &&
            !isActiveFlowScreenIndex(index))
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind != BuiltinElementKind::TabView &&
            record->kind != BuiltinElementKind::Tab)
        {
            const UINodeId tabView = tabViewStorage.tabViewForPanel(idForIndex(index));
            if (tabView.hasValue() &&
                tabViewStorage.activePanel(tabView) != idForIndex(index))
            {
                ownVisibility = UIVisibility::Collapsed;
            }
        }
        if (record->parentIndex == InvalidNodeIndex)
        {
            scratch.effectiveVisibility = ownVisibility;
        } else
        {
            const LayoutScratchState& parentScratch = layoutScratchByIndex[record->parentIndex];
            scratch.effectiveVisibility = combineVisibility(parentScratch.effectiveVisibility, ownVisibility);
        }

        const ResolvedLength width = resolveLengthNoFallbackCount(
            style.size.width, scratch.parentContentWidthDefinite, scratch.parentContentWidth);
        const ResolvedLength height = resolveLengthNoFallbackCount(
            style.size.height, scratch.parentContentHeightDefinite, scratch.parentContentHeight);
        const bool isRoot = record->parentIndex == InvalidNodeIndex;
        scratch.contentWidthDefinite = width.hasValue || isRoot;
        scratch.contentHeightDefinite = height.hasValue || isRoot;
        const float outerWidth = width.hasValue ? width.value : viewportSize.width;
        const float outerHeight = height.hasValue ? height.value : viewportSize.height;
        scratch.contentWidth =
            scratch.contentWidthDefinite ? (std::max)(0.0F, outerWidth - horizontalMargin(style.padding)) : 0.0F;
        scratch.contentHeight =
            scratch.contentHeightDefinite ? (std::max)(0.0F, outerHeight - verticalMargin(style.padding)) : 0.0F;
        scratch.hasResolvedTextMetrics = false;
        scratch.hasResolvedTextIntrinsicWidths = false;

        const LayoutPreparedInputs currentInputs{
            .effectiveVisibility = scratch.effectiveVisibility,
            .parentContentWidthDefinite = scratch.parentContentWidthDefinite,
            .parentContentHeightDefinite = scratch.parentContentHeightDefinite,
            .parentContentWidth = scratch.parentContentWidth,
            .parentContentHeight = scratch.parentContentHeight,
            .contentWidthDefinite = scratch.contentWidthDefinite,
            .contentHeightDefinite = scratch.contentHeightDefinite,
            .contentWidth = scratch.contentWidth,
            .contentHeight = scratch.contentHeight,
        };
        scratch.preparedInputs = currentInputs;

        if (allowReuse && previous != currentInputs)
        {
            // Parent constraint or effective visibility changes can
            // invalidate every descendant even when only an ancestor was
            // explicitly queued dirty.
            ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
            markLayoutAncestorsWork(index, LayoutWorkArrange);
        }
    }
}

const UILayoutStyle& UIContext::Impl::resolvedLayoutStyle(
    u32 nodeIndex) const noexcept
{
    return layoutScratchByIndex[nodeIndex].resolvedStyle;
}

Core::Result<bool> UIContext::Impl::refreshResolvedLayoutAfterArrange(
    const std::pmr::vector<u32>& order)
{
    bool changed = false;
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            continue;
        }
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const float parentWidth = record->parentIndex == InvalidNodeIndex
                                      ? scratch.parentContentWidth
                                      : layoutScratchByIndex[record->parentIndex].contentWidth;
        const UILayoutStyle responsive = resolveResponsiveLayoutStyle(
            presentationLayoutStyle(index), parentWidth);
        if (scratch.resolvedStyle != responsive)
        {
            scratch.resolvedStyle = responsive;
            changed = true;
        }

        UIVisibility ownVisibility = responsive.visibility;
        if (record->kind == BuiltinElementKind::Popup &&
            index < popupStatesByNodeIndex.size() &&
            !popupStatesByNodeIndex[index].open)
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        const UINodeId node = idForIndex(index);
        const TooltipState* tooltip =
            record->kind == BuiltinElementKind::Tooltip
                ? tooltipStorage.tryState(node)
                : nullptr;
        if (tooltip != nullptr && !tooltip->open)
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind == BuiltinElementKind::Modal &&
            dialogStorage.containsDialog(node) && !dialogStorage.isOpen(node))
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind == BuiltinElementKind::Menu &&
            menuStorage.containsMenu(node) && !menuStorage.isOpen(node))
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (index < flowStatesByNodeIndex.size() &&
            flowStatesByNodeIndex[index].kind == UIFlowNodeKind::Screen &&
            !isActiveFlowScreenIndex(index))
        {
            ownVisibility = UIVisibility::Collapsed;
        }
        if (record->kind != BuiltinElementKind::TabView &&
            record->kind != BuiltinElementKind::Tab)
        {
            const UINodeId tabView = tabViewStorage.tabViewForPanel(node);
            if (tabView.hasValue() &&
                tabViewStorage.activePanel(tabView) != node)
            {
                ownVisibility = UIVisibility::Collapsed;
            }
        }
        const UIVisibility effective =
            record->parentIndex == InvalidNodeIndex
                ? ownVisibility
                : combineVisibility(
                      layoutScratchByIndex[record->parentIndex].effectiveVisibility,
                      ownVisibility);
        if (scratch.effectiveVisibility != effective)
        {
            scratch.effectiveVisibility = effective;
            changed = true;
        }

        if (responsive.containerLayout == UIContainerLayout::Flex &&
            responsive.flexContainer.wrap == UIFlexWrap::Wrap &&
            scratch.hasMeasuredFlexWrapConstraint &&
            scratch.hasArrangedFlexWrapConstraint &&
            (scratch.measuredFlexWrapDirection !=
                 scratch.arrangedFlexWrapDirection ||
             scratch.measuredFlexWrapMain != scratch.arrangedFlexWrapMain))
        {
            changed = true;
        }

        const WidgetTextState& text = textStatesByIndex[index];
        if (text.hasContent && text.wrapMode == UITextWrapMode::Words)
        {
            const float contentWidth =
                contentPlacementFor(index).contentBox.width;
            Detail::UITextIntrinsicWidths intrinsicWidths{};
            auto metrics = measureWrappedWidgetText(
                index, contentWidth, &intrinsicWidths);
            if (!metrics)
            {
                return Core::failure(metrics.error());
            }
            if (!scratch.hasResolvedTextIntrinsicWidths ||
                scratch.resolvedTextMinContentWidth !=
                    intrinsicWidths.minContent ||
                scratch.resolvedTextMaxContentWidth !=
                    intrinsicWidths.maxContent)
            {
                scratch.resolvedTextMinContentWidth =
                    intrinsicWidths.minContent;
                scratch.resolvedTextMaxContentWidth =
                    intrinsicWidths.maxContent;
                scratch.hasResolvedTextIntrinsicWidths = true;
                changed = true;
            }
            if (!scratch.hasResolvedTextMetrics ||
                scratch.resolvedTextMetrics != *metrics)
            {
                scratch.resolvedTextMetrics = *metrics;
                scratch.hasResolvedTextMetrics = true;
                changed = true;
            }
        }
        else if (scratch.hasResolvedTextMetrics)
        {
            scratch.hasResolvedTextMetrics = false;
            changed = true;
        }
    }
    return changed;
}

void UIContext::Impl::measureLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                   LayoutPassStatistics& statistics) noexcept
{
    for (usize reverseIndex = order.size(); reverseIndex > 0; --reverseIndex)
    {
        const u32 index = order[reverseIndex - 1];
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            continue;
        }
        if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkMeasure))
        {
            continue;
        }
        const UILayoutStyle style = resolvedLayoutStyle(index);
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const UILogicalSize previousMeasuredSize = scratch.measuredSize;
        scratch.hasMeasuredFlexWrapConstraint = false;
        ++statistics.measuredNodeCount;

        if (scratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            scratch.measuredSize = {};
            scratch.minContentSize = {};
            scratch.maxContentSize = {};
            if (scratch.measuredSize != previousMeasuredSize)
            {
                ensureLayoutSubtreeWork(index, LayoutWorkArrange);
            }
            continue;
        }

        LayoutFlowMeasurement flowMeasurement{};
        LayoutFlowMeasurement minContentFlowMeasurement{};
        LayoutFlowMeasurement maxContentFlowMeasurement{};
        FlexWrapMeasurement flexWrapMeasurement{};
        GridMeasurement gridMeasurement =
            beginGridMeasurement(style.gridContainer);
        GridMeasurement minContentGridMeasurement =
            beginGridMeasurement(style.gridContainer);
        GridMeasurement maxContentGridMeasurement =
            beginGridMeasurement(style.gridContainer);

        u32 childIndex = record->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const UILayoutStyle childStyle = resolvedLayoutStyle(childIndex);
            const LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
            if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
                childStyle.placement == UILayoutPlacement::Flow)
            {
                const UILogicalSize childMinContentContribution =
                    Detail::resolveIntrinsicContribution(
                        childStyle, childScratch.minContentSize,
                        childScratch.maxContentSize, false);
                const UILogicalSize childMaxContentContribution =
                    Detail::resolveIntrinsicContribution(
                        childStyle, childScratch.minContentSize,
                        childScratch.maxContentSize, true);
                if (style.containerLayout == UIContainerLayout::Grid)
                {
                    (void)appendGridMeasuredChild(
                        gridMeasurement, style.gridContainer, childStyle,
                        childScratch.measuredSize);
                    (void)appendGridMeasuredChild(
                        minContentGridMeasurement, style.gridContainer,
                        childStyle, childMinContentContribution);
                    (void)appendGridMeasuredChild(
                        maxContentGridMeasurement, style.gridContainer,
                        childStyle, childMaxContentContribution);
                }
                else
                {
                    const bool row = style.flexContainer.direction ==
                                     UIFlexDirection::Row;
                    const float mainGap = row
                                              ? style.flexContainer.gap.column
                                              : style.flexContainer.gap.row;
                    const float crossGap = row
                                               ? style.flexContainer.gap.row
                                               : style.flexContainer.gap.column;
                    if (style.flexContainer.wrap == UIFlexWrap::Wrap)
                    {
                        appendWrappedMinContentChild(
                            minContentFlowMeasurement,
                            style.flexContainer.direction, crossGap, childStyle,
                            childMinContentContribution);
                    }
                    else
                    {
                        appendFlowMeasuredChild(
                            minContentFlowMeasurement,
                            style.flexContainer.direction, mainGap, childStyle,
                            childMinContentContribution);
                    }
                    appendFlowMeasuredChild(
                        maxContentFlowMeasurement,
                        style.flexContainer.direction, mainGap, childStyle,
                        childMaxContentContribution);
                    if (style.flexContainer.wrap == UIFlexWrap::Wrap)
                    {
                        const float availableMain =
                            row ? (scratch.contentWidthDefinite
                                       ? scratch.contentWidth : -1.0F)
                                : (scratch.contentHeightDefinite
                                       ? scratch.contentHeight : -1.0F);
                        appendFlexMeasuredItem(
                            flexWrapMeasurement,
                            style.flexContainer.direction,
                            style.flexContainer.wrap, availableMain,
                            mainGap, crossGap, childStyle, childScratch,
                            statistics);
                    }
                    else
                    {
                        appendFlowMeasuredChild(
                            flowMeasurement,
                            style.flexContainer.direction,
                            mainGap,
                            childStyle,
                            childScratch.measuredSize);
                    }
                }
            }
            childIndex = childRecord->nextSiblingIndex;
        }

        if (style.containerLayout == UIContainerLayout::Flex &&
            style.flexContainer.wrap == UIFlexWrap::Wrap)
        {
            finishFlexMeasurement(
                flexWrapMeasurement,
                style.flexContainer.direction == UIFlexDirection::Row
                    ? style.flexContainer.gap.row
                    : style.flexContainer.gap.column);
            if (flexWrapMeasurement.itemCount != 0U)
            {
                const bool row = style.flexContainer.direction ==
                                 UIFlexDirection::Row;
                float measuredMain =
                    row ? (scratch.contentWidthDefinite
                               ? scratch.contentWidth : -1.0F)
                        : (scratch.contentHeightDefinite
                               ? scratch.contentHeight : -1.0F);
                if (scratch.hasArrangedFlexWrapConstraint &&
                    scratch.arrangedFlexWrapDirection ==
                        style.flexContainer.direction)
                {
                    measuredMain = scratch.arrangedFlexWrapMain;
                }
                scratch.measuredFlexWrapDirection =
                    style.flexContainer.direction;
                scratch.measuredFlexWrapMain = measuredMain;
                scratch.hasMeasuredFlexWrapConstraint = true;
            }
        }
        usize layoutChildCount =
            style.containerLayout == UIContainerLayout::Grid
                ? gridMeasurement.childCount
                : style.flexContainer.wrap == UIFlexWrap::Wrap
                      ? flexWrapMeasurement.itemCount
                      : flowMeasurement.childCount;
        UILogicalSize layoutContentSize =
            style.containerLayout == UIContainerLayout::Grid
                ? gridMeasuredContentSize(
                      gridMeasurement, style.gridContainer)
                : style.flexContainer.wrap == UIFlexWrap::Wrap
                      ? flexWrapMeasurement.contentSize(
                            style.flexContainer.direction)
                      : flowMeasurement.contentSize;
        const UILogicalSize minContentLayoutSize =
            style.containerLayout == UIContainerLayout::Grid
                ? gridMeasuredContentSize(
                      minContentGridMeasurement, style.gridContainer)
                : minContentFlowMeasurement.contentSize;
        const UILogicalSize maxContentLayoutSize =
            style.containerLayout == UIContainerLayout::Grid
                ? gridMeasuredContentSize(
                      maxContentGridMeasurement, style.gridContainer)
                : maxContentFlowMeasurement.contentSize;
        std::optional<UILogicalSize> tabViewContentSize;
        if (record->kind == BuiltinElementKind::TabView &&
            tabViewStorage.relationshipValid(idForIndex(index)))
        {
            tabViewContentSize = UILogicalSize{};
            const UITabViewConfig& tabConfig =
                tabViewStorage.tryTabView(idForIndex(index))->config;
            const bool horizontal = tabConfig.placement == UITabViewPlacement::Top ||
                                    tabConfig.placement == UITabViewPlacement::Bottom;
            float stripMain = 0.0F;
            float stripCross = 0.0F;
            u32 stripCount = 0;
            const u32 itemCount = tabViewStorage.itemCount(idForIndex(index));
            for (u32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
            {
                const UITabViewItem item = tabViewStorage.itemAt(idForIndex(index), itemIndex);
                if (!item.hasValue())
                {
                    continue;
                }
                const LayoutScratchState& tabScratch = layoutScratchByIndex[item.tab.index()];
                const float main = horizontal ? tabScratch.measuredSize.width
                                              : tabScratch.measuredSize.height;
                const float cross = horizontal ? tabScratch.measuredSize.height
                                               : tabScratch.measuredSize.width;
                stripMain += (std::max)(0.0F, main);
                stripCross = (std::max)(stripCross, (std::max)(0.0F, cross));
                ++stripCount;
            }
            if (stripCount > 1)
            {
                stripMain += tabConfig.tabGap * static_cast<float>(stripCount - 1U);
            }
            const UINodeId panel = tabViewStorage.activePanel(idForIndex(index));
            const LayoutScratchState* panelScratch =
                panel.hasValue() ? &layoutScratchByIndex[panel.index()] : nullptr;
            const UILogicalSize panelSize = panelScratch != nullptr ? panelScratch->measuredSize
                                                                      : UILogicalSize{};
            if (horizontal)
            {
                tabViewContentSize->width = (std::max)(stripMain, panelSize.width);
                tabViewContentSize->height = stripCross +
                                             (stripCross > 0.0F ? tabConfig.contentGap : 0.0F) +
                                             panelSize.height;
            }
            else
            {
                tabViewContentSize->width = stripCross +
                                            (stripCross > 0.0F ? tabConfig.contentGap : 0.0F) +
                                            panelSize.width;
                tabViewContentSize->height = (std::max)(stripMain, panelSize.height);
            }
            layoutChildCount = 1U;
        }

        LayoutNodeMeasureContent content{.size = layoutContentSize};
        LayoutNodeMeasureContent minContent{
            .size = minContentLayoutSize,
        };
        LayoutNodeMeasureContent maxContent{
            .size = maxContentLayoutSize,
        };
        if (tabViewContentSize.has_value())
        {
            content.size = *tabViewContentSize;
            minContent.size = *tabViewContentSize;
            maxContent.size = *tabViewContentSize;
        }
        if (layoutChildCount == 0)
        {
            if (const UIImageContent* image = imageContentStorage.get(index); image != nullptr)
            {
                content.size = image->source.intrinsicLogicalSize;
                minContent.size = image->source.intrinsicLogicalSize;
                maxContent.size = image->source.intrinsicLogicalSize;
            }
        }
        if (layoutChildCount == 0 && supportsWidgetText(record->kind))
        {
            if (const UITextMetrics* metrics = presentationTextMetricsFor(index); metrics != nullptr)
            {
                const UILogicalSize textSize = metrics->measuredSize;
                content.size = textSize;
                const WidgetTextState& textState = textStatesByIndex[index];
                const UILogicalSize unwrappedTextSize =
                    textState.hasContent
                        ? textState.metrics.measuredSize
                        : textSize;
                Detail::UITextIntrinsicWidths textIntrinsicWidths{
                    .minContent = unwrappedTextSize.width,
                    .maxContent = unwrappedTextSize.width,
                };
                if (textState.hasContent &&
                    textState.wrapMode == UITextWrapMode::Words)
                {
                    textIntrinsicWidths =
                        scratch.hasResolvedTextIntrinsicWidths
                            ? Detail::UITextIntrinsicWidths{
                                  .minContent =
                                      scratch.resolvedTextMinContentWidth,
                                  .maxContent =
                                      scratch.resolvedTextMaxContentWidth,
                              }
                            : Detail::measureTextIntrinsicWidths(
                                  presentationTextViewFor(index),
                                  textState.style, textState.wrapMode, {});
                }
                minContent.size = UILogicalSize{
                    .width = textIntrinsicWidths.minContent,
                    .height = unwrappedTextSize.height,
                };
                maxContent.size = UILogicalSize{
                    .width = textIntrinsicWidths.maxContent,
                    .height = unwrappedTextSize.height,
                };
                if (record->kind == BuiltinElementKind::RadioButton &&
                    index < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
                {
                    content.indicatorLabelWidth = textSize.width;
                    content.hasIndicatorLabel = true;
                    minContent.indicatorLabelWidth =
                        minContent.size.width;
                    minContent.hasIndicatorLabel = true;
                    maxContent.indicatorLabelWidth =
                        maxContent.size.width;
                    maxContent.hasIndicatorLabel = true;
                }
            }
        }
        if (layoutChildCount == 0 && record->kind == BuiltinElementKind::Dropdown &&
            index < dropdownStatesByNodeIndex.size())
        {
            const UIDropdownPaint& dropdownPaint = dropdownStatesByNodeIndex[index].paint;
            content.size.width += dropdownPaint.indicatorWidth + dropdownPaint.indicatorInset * 2.0F;
            content.size.height = (std::max)(content.size.height, dropdownPaint.indicatorHeight);
            minContent.size.width +=
                dropdownPaint.indicatorWidth + dropdownPaint.indicatorInset * 2.0F;
            minContent.size.height = (std::max)(
                minContent.size.height, dropdownPaint.indicatorHeight);
            maxContent.size.width +=
                dropdownPaint.indicatorWidth + dropdownPaint.indicatorInset * 2.0F;
            maxContent.size.height = (std::max)(
                maxContent.size.height, dropdownPaint.indicatorHeight);
        }

        if (layoutChildCount == 0 && record->kind == BuiltinElementKind::RadioButton &&
            index < radioButtonStatesByNodeIndex.size() &&
            radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
        {
            content.leadingIndicatorExtent =
                radioButtonStatesByNodeIndex[index].paint.indicatorExtent;
            content.indicatorLabelGap = radioButtonStatesByNodeIndex[index].paint.labelGap;
            minContent.leadingIndicatorExtent =
                content.leadingIndicatorExtent;
            minContent.indicatorLabelGap = content.indicatorLabelGap;
            maxContent.leadingIndicatorExtent =
                content.leadingIndicatorExtent;
            maxContent.indicatorLabelGap = content.indicatorLabelGap;
        }

        const UILogicalSize naturalMinContent =
            Detail::intrinsicOuterSize(minContent, style.padding);
        const UILogicalSize naturalMaxContent =
            Detail::intrinsicOuterSize(maxContent, style.padding);
        scratch.minContentSize = naturalMinContent;
        scratch.maxContentSize = naturalMaxContent;
        scratch.measuredSize = resolveMeasuredLayoutSize(
            style,
            scratch,
            viewportSize,
            record->parentIndex == InvalidNodeIndex,
            content,
            statistics);
        if (scratch.measuredSize != previousMeasuredSize)
        {
            ensureLayoutSubtreeWork(index, LayoutWorkArrange);
        }
    }
}

void UIContext::Impl::assignLayoutRect(u32 index, UILogicalRect worldRect, UILogicalRect parentWorldRect,
                      UILogicalRect descendantClip) noexcept
{
    LayoutScratchState& scratch = layoutScratchByIndex[index];
    const UILayoutStyle style = resolvedLayoutStyle(index);
    const UILogicalRect previousWorldRect = scratch.worldRect;
    const UILogicalRect previousLocalRect = scratch.localRect;
    const UILogicalRect previousEffectiveClip = scratch.effectiveClip;
    const UILogicalRect previousDescendantClip = scratch.descendantClip;
    const UIVisibility previousVisibility = scratch.effectiveVisibility;
    if (scratch.effectiveVisibility == UIVisibility::Collapsed)
    {
        worldRect.width = 0.0F;
        worldRect.height = 0.0F;
    }
    scratch.worldRect = worldRect;
    scratch.localRect = UILogicalRect{
        .x = normalizeFloat(worldRect.x - parentWorldRect.x),
        .y = normalizeFloat(worldRect.y - parentWorldRect.y),
        .width = normalizeFloat(worldRect.width),
        .height = normalizeFloat(worldRect.height),
    };
    scratch.descendantClip = descendantClip;
    scratch.effectiveClip = scratch.effectiveVisibility == UIVisibility::Collapsed
                                ? UILogicalRect{}
                                : intersectRects(descendantClip, worldRect);
    scratch.contentWidthDefinite = true;
    scratch.contentHeightDefinite = true;
    scratch.contentWidth = normalizeFloat((std::max)(0.0F, worldRect.width - horizontalMargin(style.padding)));
    scratch.contentHeight = normalizeFloat((std::max)(0.0F, worldRect.height - verticalMargin(style.padding)));
    if (layoutReuseInProgress &&
        (previousWorldRect != scratch.worldRect || previousLocalRect != scratch.localRect ||
         previousEffectiveClip != scratch.effectiveClip || previousDescendantClip != scratch.descendantClip ||
         previousVisibility != scratch.effectiveVisibility))
    {
        ensureLayoutSubtreeWork(index, LayoutWorkArrange);
    }
}

void UIContext::Impl::refreshMeasuredSizeForParentContent(u32 childIndex, UILogicalRect parentContentRect,
                                         LayoutPassStatistics& statistics) noexcept
{
    const UILayoutStyle childStyle = resolvedLayoutStyle(childIndex);
    LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
    const UILogicalSize previousMeasuredSize = childScratch.measuredSize;
    childScratch.parentContentWidthDefinite = true;
    childScratch.parentContentHeightDefinite = true;
    childScratch.parentContentWidth = parentContentRect.width;
    childScratch.parentContentHeight = parentContentRect.height;

    const float resolvedOuterWidth = resolvedWidth(childStyle, childScratch, statistics);
    const float resolvedOuterHeight = resolvedHeight(childStyle, childScratch, statistics);
    float outerWidth = resolvedOuterWidth >= 0.0F
                           ? resolvedOuterWidth
                           : childScratch.measuredSize.width;
    float outerHeight = resolvedOuterHeight >= 0.0F
                            ? resolvedOuterHeight
                            : childScratch.measuredSize.height;
    Detail::applyAspectRatio(childStyle, outerWidth, outerHeight);
    childScratch.measuredSize = UILogicalSize{
        .width = clampWidth(outerWidth, childStyle, childScratch, statistics),
        .height = clampHeight(outerHeight, childStyle, childScratch, statistics),
    };
    if (layoutReuseInProgress && childScratch.measuredSize != previousMeasuredSize)
    {
        ensureLayoutSubtreeWork(childIndex, LayoutWorkArrange);
    }
}

void UIContext::Impl::arrangeOverlayChild(u32 childIndex, UILogicalRect parentContentRect, UILogicalRect parentWorldRect,
                         UILogicalRect descendantClip, LayoutPassStatistics& statistics) noexcept
{
    refreshMeasuredSizeForParentContent(childIndex, parentContentRect, statistics);
    assignLayoutRect(childIndex,
                     resolveOverlayRect(resolvedLayoutStyle(childIndex),
                                        layoutScratchByIndex[childIndex],
                                        parentContentRect, statistics),
                     parentWorldRect, descendantClip);
}

void UIContext::Impl::arrangePopupChild(u32 popupIndex, UILogicalRect anchorRect, UILogicalRect viewportRect,
                       LayoutPassStatistics& statistics) noexcept
{
    const UILayoutStyle popupLayoutStyle = resolvedLayoutStyle(popupIndex);
    LayoutScratchState& popupScratch = layoutScratchByIndex[popupIndex];
    PopupState& popup = popupStatesByNodeIndex[popupIndex];
    refreshMeasuredSizeForParentContent(popupIndex, viewportRect, statistics);
    const auto resolved = resolvePopupPlacement(
        popupLayoutStyle, popupScratch, popup.style, anchorRect,
        viewportRect, statistics);
    assignLayoutRect(popupIndex, resolved.rect, anchorRect, viewportRect);
    popupLayoutScratchByNodeIndex[popupIndex].metrics = UIPopupMetrics{
        .anchorRect = anchorRect,
        .popupRect = resolved.rect,
        .resolvedPlacement = resolved.placement,
        .open = popupScratch.effectiveVisibility == UIVisibility::Visible,
    };
}

[[nodiscard]] const UICommittedLayoutEntry*
UIContext::Impl::committedLayoutEntryFor(UINodeId node) const noexcept
{
    if (!node.hasValue())
    {
        return nullptr;
    }
    const auto& entries = committedLayoutBuffers[publishedLayoutBufferIndex];
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [node](const UICommittedLayoutEntry& entry) noexcept {
            return entry.node == node;
        });
    return found != entries.end() ? &*found : nullptr;
}

void UIContext::Impl::arrangeTooltipChild(u32 tooltipIndex, UILogicalRect parentWorldRect,
                         UILogicalRect viewportRect,
                         LayoutPassStatistics& statistics) noexcept
{
    TooltipState& tooltip = tooltipStorage.stateByIndex(tooltipIndex);
    LayoutScratchState& tooltipScratch = layoutScratchByIndex[tooltipIndex];
    const UINodeId tooltipNode = idForIndex(tooltipIndex);
    const UICommittedLayoutEntry* anchorEntry = committedLayoutEntryFor(tooltip.anchor);
    if (!tooltip.open || tooltipStorage.activeTooltip() != tooltipNode ||
        !hasValidTooltipRelationship(tooltipNode, tooltip.anchor) ||
        anchorEntry == nullptr ||
        anchorEntry->effectiveVisibility != UIVisibility::Visible)
    {
        tooltipScratch.effectiveVisibility = UIVisibility::Collapsed;
        assignLayoutRect(tooltipIndex, {}, parentWorldRect, viewportRect);
        tooltipStorage.layoutScratchByIndex(tooltipIndex) = {};
        return;
    }

    refreshMeasuredSizeForParentContent(tooltipIndex, viewportRect, statistics);
    const auto resolved = resolveTooltipPlacement(
        resolvedLayoutStyle(tooltipIndex), tooltipScratch, tooltip.config,
        anchorEntry->worldRect, viewportRect, statistics);
    assignLayoutRect(tooltipIndex, resolved.rect, parentWorldRect, viewportRect);
    tooltipStorage.layoutScratchByIndex(tooltipIndex).metrics = UITooltipMetrics{
        .anchorRect = anchorEntry->worldRect,
        .tooltipRect = resolved.rect,
        .resolvedPlacement = resolved.placement,
        .open = tooltipScratch.effectiveVisibility == UIVisibility::Visible,
    };
}

void UIContext::Impl::arrangeMenuChild(u32 menuIndex, UILogicalRect parentWorldRect,
                      UILogicalRect viewportRect,
                      LayoutPassStatistics& statistics) noexcept
{
    MenuState* menu = menuStorage.tryMenu(idForIndex(menuIndex));
    LayoutScratchState& menuScratch = layoutScratchByIndex[menuIndex];
    const UINodeId menuNode = idForIndex(menuIndex);
    const UINodeId anchor = menuPlacementAnchor(menuNode);
    const UICommittedLayoutEntry* anchorEntry = committedLayoutEntryFor(anchor);
    if (menu == nullptr || !menuStorage.isOpen(menuNode) ||
        !hasValidMenuPlacementRelationship(menuNode, anchor) || anchorEntry == nullptr ||
        anchorEntry->effectiveVisibility != UIVisibility::Visible ||
        !isNodeEnabled(menuNode) || !isNodeEnabled(anchor) ||
        !isAuthoredTooltipNodeVisible(menuNode) ||
        !isAuthoredTooltipNodeVisible(anchor))
    {
        menuScratch.effectiveVisibility = UIVisibility::Collapsed;
        assignLayoutRect(menuIndex, {}, parentWorldRect, viewportRect);
        menuStorage.layoutScratchByIndex(menuIndex) = {};
        return;
    }

    refreshMeasuredSizeForParentContent(menuIndex, viewportRect, statistics);
    const UILogicalRect placementAnchorRect =
        menuStorage.hasInvocationAnchorRect(menuNode)
            ? menuStorage.invocationAnchorRect(menuNode)
            : anchorEntry->worldRect;
    UIMenuConfig placementConfig = menu->config;
    if (menuStorage.parentItemForMenu(menuNode).hasValue() &&
        placementConfig.placement == UIMenuPlacement::Auto)
    {
        placementConfig.placement = UIMenuPlacement::Right;
    }
    const auto resolved = resolveMenuPlacement(
        resolvedLayoutStyle(menuIndex), menuScratch, placementConfig,
        placementAnchorRect, viewportRect, statistics);
    assignLayoutRect(menuIndex, resolved.rect, parentWorldRect, viewportRect);
    menuStorage.layoutScratchByIndex(menuIndex).metrics = UIMenuMetrics{
        .anchorRect = placementAnchorRect,
        .menuRect = resolved.rect,
        .resolvedPlacement = resolved.placement,
        .open = menuScratch.effectiveVisibility == UIVisibility::Visible,
    };
}

[[nodiscard]] Core::Status UIContext::Impl::bindListViewItem(u32 itemIndex, u64 logicalIndex,
                                            const UIListViewItemDescriptor& descriptor)
{
    if (itemIndex >= textStatesByIndex.size() || itemIndex >= listViewItemStatesByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI ListView item side state is out of range");
    }
    if (descriptor.key == InvalidUIListViewItemKey || containsLineBreak(descriptor.label))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView item requires a non-zero key and a single-line label");
    }
    if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI ListView item label is too large");
    }

    WidgetTextState& text = textStatesByIndex[itemIndex];
    auto metrics = measureWidgetText(descriptor.label, text.style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }

    TextByteAllocation replacement{};
    bool replaceAllocation = false;
    if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
    {
        auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        replacement = *allocation;
        replaceAllocation = true;
    }
    if (descriptor.label.empty())
    {
        textStorage.release(text.allocation);
        text.allocation = {};
        text.length = 0;
        text.metrics = {};
        text.hasContent = false;
    } else
    {
        if (replaceAllocation)
        {
            textStorage.release(text.allocation);
            text.allocation = replacement;
        }
        textStorage.write(text.allocation, descriptor.label);
        text.length = static_cast<u32>(descriptor.label.size());
        text.metrics = *metrics;
        text.hasContent = true;
    }
    localTextColorCacheByIndex[itemIndex] = text.hasContent ? premultiply(text.style.color)
                                                           : UIPremultipliedRgba8Color{};
    ListViewItemState& item = listViewItemStatesByNodeIndex[itemIndex];
    item.key = descriptor.key;
    item.logicalIndex = logicalIndex;
    item.bound = true;
    item.enabled = descriptor.enabled;
    return Core::success();
}

void UIContext::Impl::collapseListViewItems(u32 listViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                           UILogicalRect descendantClip) noexcept
{
    const NodeRecord* listRecord = recordByIndex(listViewIndex);
    u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        ListViewItemState& item = listViewItemStatesByNodeIndex[childIndex];
        item.key = InvalidUIListViewItemKey;
        item.logicalIndex = 0;
        item.bound = false;
        item.enabled = true;
        layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
        assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
        childIndex = childRecord->nextSiblingIndex;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeListViewItems(u32 listViewIndex, UILogicalRect unscrolledContentRect,
                                                UILogicalRect parentWorldRect, UILogicalRect descendantClip)
{
    ListViewState& state = listViewStatesByNodeIndex[listViewIndex];
    const u64 logicalItemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
    const auto collectionLayout = resolveVirtualCollectionLayout({
        .logicalItemCount = logicalItemCount,
        .materializedItemCapacity = state.materializedItemCapacity,
        .rowHeight = state.style.rowHeight,
        .overscanRows = state.style.overscanRows,
        .scrollBarVisibility = state.style.scrollBarVisibility,
        .scrollBarThickness = state.paint.scrollBar.thickness,
        .requestedScrollOffset = state.requestedScrollOffset,
        .availableRect = unscrolledContentRect,
    });
    if (!collectionLayout)
    {
        return collectionLayout.error() == VirtualCollectionLayoutError::ContentHeightNotRepresentable
                   ? fail(UIErrorCode::InvalidControlValue,
                          "UI ListView logical content height is not representable")
                   : fail(UIErrorCode::CapacityExceeded,
                          "UI ListView row pool cannot cover the viewport and configured overscan");
    }
    const auto& plan = *collectionLayout;

    ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[listViewIndex];
    listLayout = ListViewLayoutScratch{
        .metrics =
            UIListViewMetrics{
                .logicalItemCount = logicalItemCount,
                .firstVisibleIndex = plan.firstVisibleIndex,
                .visibleItemCount = static_cast<u32>(plan.visibleItemCount),
                .firstMaterializedIndex = plan.firstMaterializedIndex,
                .materializedItemCount = static_cast<u32>(plan.materializedItemCount),
                .materializedItemCapacity = state.materializedItemCapacity,
                .scrollOffset = plan.scrollOffset,
                .maxScrollOffset = plan.maximumScrollOffset,
                .viewportSize = plan.viewportRect.size(),
                .contentSize = plan.contentSize,
                .verticalScrollBarVisible = plan.verticalScrollBarVisible,
            },
        .viewportRect = plan.viewportRect,
    };

    const NodeRecord* listRecord = recordByIndex(listViewIndex);
    u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
    u64 materializedOrdinal = 0;
    const UIVisibility rowVisibility = layoutScratchByIndex[listViewIndex].effectiveVisibility;
    const UILogicalRect rowClip = intersectRects(descendantClip, plan.viewportRect);
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        const u32 currentChild = childIndex;
        childIndex = childRecord->nextSiblingIndex;
        if (materializedOrdinal >= plan.materializedItemCount)
        {
            ListViewItemState& item = listViewItemStatesByNodeIndex[currentChild];
            item.key = InvalidUIListViewItemKey;
            item.logicalIndex = 0;
            item.bound = false;
            item.enabled = true;
            layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(currentChild, plan.viewportRect, parentWorldRect, rowClip);
            continue;
        }

        const u64 logicalIndex = plan.firstMaterializedIndex + materializedOrdinal;
        auto descriptor = resolveListViewLogicalItem(idForIndex(listViewIndex), logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        if (Core::Status bound = bindListViewItem(currentChild, logicalIndex, *descriptor); !bound)
        {
            return bound;
        }
        if (textStatesByIndex[currentChild].metrics.measuredSize.height >
            state.style.rowHeight + CollectionRowTextFitTolerance)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView row height is smaller than its text line box");
        }
        configureCollectionRowLayout(layoutStylesByIndex[currentChild], state.style.rowHeight);
        layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
        const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
        const float rowY = normalizeFloat(
            plan.viewportRect.y + static_cast<float>(logicalY) - plan.scrollOffset);
        assignLayoutRect(currentChild,
                         UILogicalRect{
                             .x = plan.viewportRect.x,
                             .y = rowY,
                             .width = plan.viewportRect.width,
                             .height = state.style.rowHeight,
                         },
                         parentWorldRect, rowClip);
        ++materializedOrdinal;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::bindVirtualGridViewItem(
    UINodeId item, u64 logicalIndex,
    const UIVirtualGridViewItemDescriptor& descriptor)
{
    if (item.index() >= textStatesByIndex.size() ||
        virtualGridViewStorage.tryItem(item) == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView item side state is out of range");
    }
    if (descriptor.key == InvalidUIVirtualGridViewItemKey ||
        containsLineBreak(descriptor.label) ||
        containsLineBreak(descriptor.presentation.secondaryLabel) ||
        containsLineBreak(descriptor.presentation.statusLabel))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView item requires non-zero key and single-line UTF-8 labels");
    }
    if (descriptor.label.size() > (std::numeric_limits<u32>::max)() ||
        descriptor.presentation.secondaryLabel.size() > (std::numeric_limits<u32>::max)() ||
        descriptor.presentation.statusLabel.size() > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI VirtualGridView item labels are too large");
    }
    if (!Core::isStrictUtf8WithoutNul(descriptor.label) ||
        !Core::isStrictUtf8WithoutNul(descriptor.presentation.secondaryLabel) ||
        !Core::isStrictUtf8WithoutNul(descriptor.presentation.statusLabel))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI VirtualGridView item labels must be strict UTF-8 without NUL");
    }

    WidgetTextState& text = textStatesByIndex[item.index()];
    auto metrics = measureWidgetText(descriptor.label, text.style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }

    TextByteAllocation replacement{};
    bool replaceAllocation = false;
    if (!descriptor.label.empty() &&
        text.allocation.capacity < descriptor.label.size())
    {
        auto allocation =
            textStorage.allocate(static_cast<u32>(descriptor.label.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        replacement = *allocation;
        replaceAllocation = true;
    }
    if (descriptor.label.empty())
    {
        textStorage.release(text.allocation);
        text.allocation = {};
        text.length = 0;
        text.metrics = {};
        text.hasContent = false;
    }
    else
    {
        if (replaceAllocation)
        {
            textStorage.release(text.allocation);
            text.allocation = replacement;
        }
        textStorage.write(text.allocation, descriptor.label);
        text.length = static_cast<u32>(descriptor.label.size());
        text.metrics = *metrics;
        text.hasContent = true;
    }
    localTextColorCacheByIndex[item.index()] =
        text.hasContent ? premultiply(text.style.color)
                        : UIPremultipliedRgba8Color{};
    if (!virtualGridViewStorage.bindItem(
            item, descriptor.key, logicalIndex, descriptor.enabled,
            descriptor.presentation))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView item binding failed");
    }

    // Preview/icon metadata reuses the existing retained Image content
    // slot. Normalize before mutating storage, then replace in place so a
    // later candidate failure can roll back to the previous image.
    const std::optional<UIImageSource>& source =
        descriptor.presentation.preview.has_value()
            ? descriptor.presentation.preview
            : descriptor.presentation.icon;
    if (source.has_value())
    {
        const UIImageContent image{
            .source = *source,
            .fit = UIImageFit::Contain,
            .alignment = {
                .horizontal = UIAxisAlignment::Center,
                .vertical = UIAxisAlignment::Center,
            },
            .tint = rgba8(255, 255, 255),
            .sampling = UIImageSampling::Linear,
        };
        auto normalized = normalizeImageContent(image);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        if (Core::Status assigned = imageContentStorage.replace(item.index(), *normalized); !assigned)
        {
            return assigned;
        }
    }
    else
    {
        imageContentStorage.release(item.index());
    }
    return Core::success();
}

void UIContext::Impl::collapseVirtualGridViewItems(
    UINodeId virtualGridView, UILogicalRect contentRect,
    UILogicalRect parentWorldRect, UILogicalRect descendantClip) noexcept
{
    if (VirtualGridViewLayoutScratch* scratch =
            virtualGridViewStorage.tryLayoutScratch(virtualGridView))
    {
        *scratch = {};
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr)
    {
        return;
    }
    UINodeId item = state->firstMaterializedItem;
    for (u32 visited = 0;
         item.hasValue() && visited < state->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* itemState =
            virtualGridViewStorage.tryItem(item);
        if (itemState == nullptr ||
            itemState->virtualGridView != virtualGridView)
        {
            break;
        }
        const UINodeId next = itemState->nextItem;
        virtualGridViewStorage.clearItemBinding(item);
        layoutScratchByIndex[item.index()].effectiveVisibility =
            UIVisibility::Collapsed;
        assignLayoutRect(
            item.index(), contentRect, parentWorldRect, descendantClip);
        item = next;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeVirtualGridViewItems(
    UINodeId virtualGridView, UILogicalRect unscrolledContentRect,
    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
{
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    VirtualGridViewLayoutScratch* layout =
        virtualGridViewStorage.tryLayoutScratch(virtualGridView);
    if (state == nullptr || layout == nullptr ||
        !virtualGridViewStorage.relationshipValid(virtualGridView))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView item pool relationship is invalid");
    }
    const u64 logicalItemCount = state->dataSource.hasValue()
                                     ? state->dataSource.itemCount(
                                           state->dataSource.state)
                                     : 0;
    const auto resolved = resolveVirtualGridLayout({
        .logicalItemCount = logicalItemCount,
        .materializedItemCapacity = state->materializedItemCapacity,
        .minimumItemWidth = state->style.minimumItemWidth,
        .itemHeight = state->style.itemHeight,
        .columnGap = state->style.columnGap,
        .rowGap = state->style.rowGap,
        .maximumColumnCount = state->style.maximumColumnCount,
        .stretchLastRow = state->style.stretchLastRow,
        .overscanRows = state->style.overscanRows,
        .scrollBarVisibility = state->style.scrollBarVisibility,
        .scrollBarThickness = state->paint.scrollBar.thickness,
        .requestedScrollOffset = state->requestedScrollOffset,
        .availableRect = unscrolledContentRect,
    });
    if (!resolved)
    {
        switch (resolved.error())
        {
        case VirtualGridLayoutError::ContentHeightNotRepresentable:
            return fail(UIErrorCode::InvalidControlValue,
                        "UI VirtualGridView logical content height is not representable");
        case VirtualGridLayoutError::MaterializedRangeExceedsCapacity:
            return fail(UIErrorCode::CapacityExceeded,
                        "UI VirtualGridView item pool cannot cover the viewport and configured overscan");
        case VirtualGridLayoutError::InvalidGeometry:
            return fail(UIErrorCode::InvalidControlValue,
                        "UI VirtualGridView layout geometry is invalid");
        }
    }
    const auto& plan = *resolved;
    *layout = VirtualGridViewLayoutScratch{
        .metrics =
            UIVirtualGridViewMetrics{
                .logicalItemCount = logicalItemCount,
                .logicalRowCount = plan.logicalRowCount,
                .logicalColumnCount = plan.logicalColumnCount,
                .firstVisibleRow = plan.firstVisibleRow,
                .visibleRowCount = static_cast<u32>(plan.visibleRowCount),
                .firstMaterializedRow = plan.firstMaterializedRow,
                .materializedRowCount =
                    static_cast<u32>(plan.materializedRowCount),
                .firstMaterializedIndex = plan.firstMaterializedIndex,
                .materializedItemCount =
                    static_cast<u32>(plan.materializedItemCount),
                .materializedItemCapacity =
                    state->materializedItemCapacity,
                .itemWidth = plan.itemWidth,
                .scrollOffset = plan.scrollOffset,
                .maxScrollOffset = plan.maximumScrollOffset,
                .viewportSize = plan.viewportRect.size(),
                .contentSize = plan.contentSize,
                .verticalScrollBarVisible =
                    plan.verticalScrollBarVisible,
            },
        .viewportRect = plan.viewportRect,
    };

    const UIVisibility itemVisibility =
        layoutScratchByIndex[virtualGridView.index()].effectiveVisibility;
    const UILogicalRect itemClip =
        intersectRects(descendantClip, plan.viewportRect);
    UINodeId item = state->firstMaterializedItem;
    u64 materializedOrdinal = 0;
    for (u32 visited = 0;
         item.hasValue() && visited < state->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* itemState =
            virtualGridViewStorage.tryItem(item);
        if (itemState == nullptr ||
            itemState->virtualGridView != virtualGridView)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI VirtualGridView item pool traversal failed");
        }
        const UINodeId next = itemState->nextItem;
        if (materializedOrdinal >= plan.materializedItemCount)
        {
            virtualGridViewStorage.clearItemBinding(item);
            layoutScratchByIndex[item.index()].effectiveVisibility =
                UIVisibility::Collapsed;
            assignLayoutRect(
                item.index(), plan.viewportRect, parentWorldRect, itemClip);
            item = next;
            continue;
        }

        const u64 logicalIndex =
            plan.firstMaterializedIndex + materializedOrdinal;
        auto descriptor = resolveVirtualGridViewLogicalItem(
            virtualGridView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        if (Core::Status bound = bindVirtualGridViewItem(
                item, logicalIndex, *descriptor);
            !bound)
        {
            return bound;
        }
        if (textStatesByIndex[item.index()].metrics.measuredSize.height >
            state->style.itemHeight + CollectionRowTextFitTolerance)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI VirtualGridView item height is smaller than its text line box");
        }
        configureCollectionRowLayout(
            layoutStylesByIndex[item.index()], state->style.itemHeight);
        layoutScratchByIndex[item.index()].effectiveVisibility =
            itemVisibility;
        assignLayoutRect(
            item.index(), resolveVirtualGridItemRect(plan, logicalIndex),
            parentWorldRect, itemClip);
        ++materializedOrdinal;
        item = next;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::bindDataGridText(
    UINodeId node, std::string_view value)
{
    if (node.index() >= textStatesByIndex.size() || containsLineBreak(value))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid text must be a single line");
    }
    if (value.size() > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid text is too large");
    }
    WidgetTextState& text = textStatesByIndex[node.index()];
    auto metrics = measureWidgetText(value, text.style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }
    TextByteAllocation replacement{};
    bool replaceAllocation = false;
    if (!value.empty() && text.allocation.capacity < value.size())
    {
        auto allocation = textStorage.allocate(static_cast<u32>(value.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        replacement = *allocation;
        replaceAllocation = true;
    }
    if (value.empty())
    {
        textStorage.release(text.allocation);
        text = WidgetTextState{.style = text.style, .overflow = text.overflow};
    }
    else
    {
        if (replaceAllocation)
        {
            textStorage.release(text.allocation);
            text.allocation = replacement;
        }
        textStorage.write(text.allocation, value);
        text.length = static_cast<u32>(value.size());
        text.metrics = *metrics;
        text.hasContent = true;
    }
    localTextColorCacheByIndex[node.index()] =
        text.hasContent ? premultiply(text.style.color)
                        : UIPremultipliedRgba8Color{};
    return Core::success();
}

void UIContext::Impl::collapseDataGrid(
    UINodeId dataGrid, UILogicalRect contentRect,
    UILogicalRect parentWorldRect, UILogicalRect descendantClip) noexcept
{
    DataGridLayoutScratch* layout = dataGridStorage.tryLayoutScratch(dataGrid);
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (layout != nullptr)
    {
        *layout = {};
    }
    if (state == nullptr)
    {
        return;
    }
    dataGridStorage.clearColumnBindings(dataGrid);
    dataGridStorage.clearRowBindings(dataGrid);
    for (u32 column = 0; column < state->linkedColumnCount; ++column)
    {
        const UINodeId header = dataGridStorage.columnAt(dataGrid, column);
        if (!header.hasValue())
        {
            continue;
        }
        layoutScratchByIndex[header.index()].effectiveVisibility =
            UIVisibility::Collapsed;
        assignLayoutRect(header.index(), contentRect, parentWorldRect,
                         descendantClip);
    }
    for (u32 rowOrdinal = 0;
         rowOrdinal < state->linkedMaterializedRowCount; ++rowOrdinal)
    {
        const UINodeId row = dataGridStorage.rowAt(dataGrid, rowOrdinal);
        if (!row.hasValue())
        {
            continue;
        }
        layoutScratchByIndex[row.index()].effectiveVisibility =
            UIVisibility::Collapsed;
        assignLayoutRect(row.index(), contentRect, parentWorldRect,
                         descendantClip);
        for (u32 column = 0; column < state->linkedColumnCount; ++column)
        {
            const UINodeId cell =
                dataGridStorage.cellAt(dataGrid, rowOrdinal, column);
            if (!cell.hasValue())
            {
                continue;
            }
            layoutScratchByIndex[cell.index()].effectiveVisibility =
                UIVisibility::Collapsed;
            assignLayoutRect(cell.index(), contentRect, contentRect,
                             descendantClip);
        }
    }
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeDataGrid(
    UINodeId dataGrid, UILogicalRect unscrolledContentRect,
    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
{
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    DataGridLayoutScratch* layout = dataGridStorage.tryLayoutScratch(dataGrid);
    if (state == nullptr || layout == nullptr ||
        !dataGridStorage.relationshipValid(dataGrid))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI DataGrid fixed-pool relationship is invalid");
    }
    const u64 rowCount = state->dataSource.hasValue()
                             ? state->dataSource.rowCount(state->dataSource.state)
                             : 0;
    const u32 columnCount = state->dataSource.hasValue()
                                ? state->dataSource.columnCount(state->dataSource.state)
                                : 0;
    if (columnCount > state->columnCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid logical columns exceed the fixed column pool");
    }

    dataGridColumnWidthScratch.clear();
    for (u32 logicalColumn = 0; logicalColumn < columnCount;
         ++logicalColumn)
    {
        UIDataGridColumnDescriptor descriptor{};
        if (!state->dataSource.resolveColumn(
                state->dataSource.state, logicalColumn, descriptor) ||
            descriptor.key == InvalidUIDataGridColumnKey ||
            !std::isfinite(descriptor.width) || descriptor.width <= 0.0F ||
            containsLineBreak(descriptor.header))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI DataGrid column descriptor is invalid");
        }
        const UINodeId header = dataGridStorage.columnAt(
            dataGrid, logicalColumn);
        if (!header.hasValue() ||
            !dataGridStorage.bindColumn(
                header, descriptor.key, descriptor.width))
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI DataGrid column binding failed");
        }
        if (Core::Status text = bindDataGridText(header, descriptor.header);
            !text)
        {
            return text;
        }
        dataGridColumnWidthScratch.push_back(descriptor.width);
    }
    for (u32 column = columnCount; column < state->columnCapacity; ++column)
    {
        const UINodeId header = dataGridStorage.columnAt(dataGrid, column);
        if (DataGridColumnState* columnState =
                dataGridStorage.tryColumn(header))
        {
            columnState->key = InvalidUIDataGridColumnKey;
            columnState->width = 0.0F;
            columnState->bound = false;
        }
    }

    const auto resolved = Detail::resolveDataGridLayout({
        .logicalRowCount = rowCount,
        .columnCapacity = state->columnCapacity,
        .materializedRowCapacity = state->materializedRowCapacity,
        .columnWidths = dataGridColumnWidthScratch,
        .columnHeaderHeight = state->style.columnHeaderHeight,
        .rowHeight = state->style.rowHeight,
        .overscanRows = state->style.overscanRows,
        .scrollBarVisibility = state->style.scrollBarVisibility,
        .scrollBarThickness = state->paint.scrollBar.thickness,
        .requestedOffset = state->requestedScrollOffset,
        .availableRect = unscrolledContentRect,
    });
    if (!resolved)
    {
        return resolved.error() == Detail::DataGridLayoutError::MaterializedRangeExceedsCapacity
                   ? fail(UIErrorCode::CapacityExceeded,
                          "UI DataGrid row pool cannot cover the viewport and configured overscan")
                   : fail(UIErrorCode::InvalidControlValue,
                          "UI DataGrid layout geometry or logical extent is invalid");
    }
    const auto& plan = *resolved;
    *layout = DataGridLayoutScratch{
        .metrics = UIDataGridMetrics{
            .logicalRowCount = rowCount,
            .logicalColumnCount = columnCount,
            .firstVisibleRow = plan.firstVisibleRow,
            .visibleRowCount = static_cast<u32>(plan.visibleRowCount),
            .firstMaterializedRow = plan.firstMaterializedRow,
            .materializedRowCount = static_cast<u32>(plan.materializedRowCount),
            .materializedRowCapacity = state->materializedRowCapacity,
            .columnCapacity = state->columnCapacity,
            .scrollOffset = plan.scrollOffset,
            .maxScrollOffset = plan.maximumScrollOffset,
            .viewportSize = plan.bodyViewportRect.size(),
            .contentSize = plan.contentSize,
            .horizontalScrollBarVisible = plan.horizontalScrollBarVisible,
            .verticalScrollBarVisible = plan.verticalScrollBarVisible,
        },
        .headerViewportRect = plan.headerViewportRect,
        .bodyViewportRect = plan.bodyViewportRect,
    };

    const UIVisibility visibility =
        layoutScratchByIndex[dataGrid.index()].effectiveVisibility;
    const UILogicalRect headerClip =
        intersectRects(descendantClip, plan.headerViewportRect);
    const UILogicalRect bodyClip =
        intersectRects(descendantClip, plan.bodyViewportRect);
    for (u32 column = 0; column < state->columnCapacity; ++column)
    {
        const UINodeId header = dataGridStorage.columnAt(dataGrid, column);
        const bool bound = column < columnCount;
        layoutScratchByIndex[header.index()].effectiveVisibility =
            bound ? visibility : UIVisibility::Collapsed;
        const UILogicalRect rect = bound
                                       ? Detail::resolveDataGridHeaderCellRect(
                                             plan, dataGridColumnWidthScratch,
                                             column)
                                       : plan.headerViewportRect;
        assignLayoutRect(header.index(), rect, parentWorldRect, headerClip);
    }

    for (u32 rowOrdinal = 0;
         rowOrdinal < state->materializedRowCapacity; ++rowOrdinal)
    {
        const UINodeId row = dataGridStorage.rowAt(dataGrid, rowOrdinal);
        const bool materialized = rowOrdinal < plan.materializedRowCount;
        if (!materialized)
        {
            if (DataGridRowState* rowState = dataGridStorage.tryRow(row))
            {
                rowState->key = InvalidUIDataGridRowKey;
                rowState->logicalRow = 0;
                rowState->bound = false;
                rowState->enabled = true;
            }
            layoutScratchByIndex[row.index()].effectiveVisibility =
                UIVisibility::Collapsed;
            assignLayoutRect(row.index(), plan.bodyViewportRect,
                             parentWorldRect, bodyClip);
            for (u32 column = 0; column < state->columnCapacity; ++column)
            {
                const UINodeId cell = dataGridStorage.cellAt(
                    dataGrid, rowOrdinal, column);
                if (DataGridCellState* cellState =
                        dataGridStorage.tryCell(cell))
                {
                    cellState->bound = false;
                }
                layoutScratchByIndex[cell.index()].effectiveVisibility =
                    UIVisibility::Collapsed;
                assignLayoutRect(cell.index(), plan.bodyViewportRect,
                                 plan.bodyViewportRect, bodyClip);
            }
            continue;
        }

        const u64 logicalRow = plan.firstMaterializedRow + rowOrdinal;
        UIDataGridRowDescriptor rowDescriptor{};
        if (!state->dataSource.resolveRow(
                state->dataSource.state, logicalRow, rowDescriptor) ||
            rowDescriptor.key == InvalidUIDataGridRowKey ||
            !dataGridStorage.bindRow(
                row, rowDescriptor.key, logicalRow, rowDescriptor.enabled))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI DataGrid row descriptor is invalid");
        }
        const UILogicalRect rowRect{
            .x = plan.bodyContentRect.x,
            .y = normalizeFloat(plan.bodyContentRect.y +
                                static_cast<float>(logicalRow) * plan.rowHeight),
            .width = plan.contentSize.width,
            .height = plan.rowHeight,
        };
        layoutScratchByIndex[row.index()].effectiveVisibility = visibility;
        assignLayoutRect(row.index(), rowRect, parentWorldRect, bodyClip);
        for (u32 column = 0; column < state->columnCapacity; ++column)
        {
            const UINodeId cell = dataGridStorage.cellAt(
                dataGrid, rowOrdinal, column);
            if (column >= columnCount)
            {
                if (DataGridCellState* cellState =
                        dataGridStorage.tryCell(cell))
                {
                    cellState->bound = false;
                }
                layoutScratchByIndex[cell.index()].effectiveVisibility =
                    UIVisibility::Collapsed;
                assignLayoutRect(cell.index(), rowRect, rowRect, bodyClip);
                continue;
            }
            UIDataGridCellDescriptor cellDescriptor{};
            if (!state->dataSource.resolveCell(
                    state->dataSource.state, logicalRow, column,
                    cellDescriptor) || containsLineBreak(cellDescriptor.text) ||
                !dataGridStorage.bindCell(cell, logicalRow, column))
            {
                return fail(UIErrorCode::InvalidControlValue,
                            "UI DataGrid cell descriptor is invalid");
            }
            if (Core::Status text =
                    bindDataGridText(cell, cellDescriptor.text);
                !text)
            {
                return text;
            }
            layoutScratchByIndex[cell.index()].effectiveVisibility = visibility;
            assignLayoutRect(
                cell.index(), Detail::resolveDataGridCellRect(
                                  plan, dataGridColumnWidthScratch,
                                  logicalRow, column),
                rowRect, bodyClip);
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::bindTreeViewItem(u32 itemIndex, u64 logicalIndex,
                                            const UITreeViewItemDescriptor& descriptor)
{
    if (itemIndex >= textStatesByIndex.size() || itemIndex >= treeViewItemStatesByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI TreeView item side state is out of range");
    }
    if (descriptor.key == InvalidUITreeViewItemKey || containsLineBreak(descriptor.label) ||
        (descriptor.expanded && !descriptor.expandable))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView item requires a non-zero key, a single-line label, and valid expansion state");
    }
    if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI TreeView item label is too large");
    }

    WidgetTextState& text = textStatesByIndex[itemIndex];
    auto metrics = measureWidgetText(descriptor.label, text.style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }

    TextByteAllocation replacement{};
    bool replaceAllocation = false;
    if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
    {
        auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        replacement = *allocation;
        replaceAllocation = true;
    }
    if (descriptor.label.empty())
    {
        textStorage.release(text.allocation);
        text.allocation = {};
        text.length = 0;
        text.metrics = {};
        text.hasContent = false;
    } else
    {
        if (replaceAllocation)
        {
            textStorage.release(text.allocation);
            text.allocation = replacement;
        }
        textStorage.write(text.allocation, descriptor.label);
        text.length = static_cast<u32>(descriptor.label.size());
        text.metrics = *metrics;
        text.hasContent = true;
    }
    localTextColorCacheByIndex[itemIndex] =
        text.hasContent ? premultiply(text.style.color) : UIPremultipliedRgba8Color{};
    TreeViewItemState& item = treeViewItemStatesByNodeIndex[itemIndex];
    item.key = descriptor.key;
    item.logicalIndex = logicalIndex;
    item.level = descriptor.level;
    item.bound = true;
    item.enabled = descriptor.enabled;
    item.expandable = descriptor.expandable;
    item.expanded = descriptor.expanded;
    return Core::success();
}

void UIContext::Impl::collapseTreeViewItems(u32 treeViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                           UILogicalRect descendantClip) noexcept
{
    const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
    u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        TreeViewItemState& item = treeViewItemStatesByNodeIndex[childIndex];
        item = {};
        layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
        assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
        childIndex = childRecord->nextSiblingIndex;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeTreeViewItems(u32 treeViewIndex, UILogicalRect unscrolledContentRect,
                                                UILogicalRect parentWorldRect, UILogicalRect descendantClip)
{
    TreeViewState& state = treeViewStatesByNodeIndex[treeViewIndex];
    const u64 logicalItemCount =
        state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
    const auto collectionLayout = resolveVirtualCollectionLayout({
        .logicalItemCount = logicalItemCount,
        .materializedItemCapacity = state.materializedItemCapacity,
        .rowHeight = state.style.rowHeight,
        .overscanRows = state.style.overscanRows,
        .scrollBarVisibility = state.style.scrollBarVisibility,
        .scrollBarThickness = state.paint.scrollBar.thickness,
        .requestedScrollOffset = state.requestedScrollOffset,
        .availableRect = unscrolledContentRect,
    });
    if (!collectionLayout)
    {
        return collectionLayout.error() == VirtualCollectionLayoutError::ContentHeightNotRepresentable
                   ? fail(UIErrorCode::InvalidControlValue,
                          "UI TreeView logical content height is not representable")
                   : fail(UIErrorCode::CapacityExceeded,
                          "UI TreeView row pool cannot cover the viewport and configured overscan");
    }
    const auto& plan = *collectionLayout;

    TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[treeViewIndex];
    treeLayout = TreeViewLayoutScratch{
        .metrics =
            UITreeViewMetrics{
                .logicalItemCount = logicalItemCount,
                .firstVisibleIndex = plan.firstVisibleIndex,
                .visibleItemCount = static_cast<u32>(plan.visibleItemCount),
                .firstMaterializedIndex = plan.firstMaterializedIndex,
                .materializedItemCount = static_cast<u32>(plan.materializedItemCount),
                .materializedItemCapacity = state.materializedItemCapacity,
                .scrollOffset = plan.scrollOffset,
                .maxScrollOffset = plan.maximumScrollOffset,
                .viewportSize = plan.viewportRect.size(),
                .contentSize = plan.contentSize,
                .verticalScrollBarVisible = plan.verticalScrollBarVisible,
            },
        .viewportRect = plan.viewportRect,
    };

    const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
    u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
    u64 materializedOrdinal = 0;
    const UIVisibility rowVisibility = layoutScratchByIndex[treeViewIndex].effectiveVisibility;
    const UILogicalRect rowClip = intersectRects(descendantClip, plan.viewportRect);
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        const u32 currentChild = childIndex;
        childIndex = childRecord->nextSiblingIndex;
        if (materializedOrdinal >= plan.materializedItemCount)
        {
            treeViewItemStatesByNodeIndex[currentChild] = {};
            layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(currentChild, plan.viewportRect, parentWorldRect, rowClip);
            continue;
        }

        const u64 logicalIndex = plan.firstMaterializedIndex + materializedOrdinal;
        auto descriptor = resolveTreeViewLogicalItem(idForIndex(treeViewIndex), logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        const double leftPadding = 8.0 + static_cast<double>(descriptor->level) * state.style.indentation +
                                   state.style.disclosureExtent + state.style.disclosureGap;
        if (!std::isfinite(leftPadding) || leftPadding > (std::numeric_limits<float>::max)())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView item indentation is not representable");
        }
        if (Core::Status bound = bindTreeViewItem(currentChild, logicalIndex, *descriptor); !bound)
        {
            return bound;
        }
        UILayoutStyle& rowStyle = layoutStylesByIndex[currentChild];
        if (textStatesByIndex[currentChild].metrics.measuredSize.height >
            state.style.rowHeight + CollectionRowTextFitTolerance)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView row height is smaller than its text line box");
        }
        configureCollectionRowLayout(rowStyle, state.style.rowHeight,
                                     normalizeFloat(static_cast<float>(leftPadding)));
        layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
        const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
        const float rowY = normalizeFloat(
            plan.viewportRect.y + static_cast<float>(logicalY) - plan.scrollOffset);
        assignLayoutRect(currentChild,
                         UILogicalRect{
                             .x = plan.viewportRect.x,
                             .y = rowY,
                             .width = plan.viewportRect.width,
                             .height = state.style.rowHeight,
                         },
                         parentWorldRect, rowClip);
        ++materializedOrdinal;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeChildren(u32 parentIndex, UILogicalRect viewportRect,
                                           LayoutPassStatistics& statistics)
{
    const NodeRecord* parentRecord = recordByIndex(parentIndex);
    if (parentRecord == nullptr)
    {
        return Core::success();
    }
    const UILayoutStyle parentStyle = resolvedLayoutStyle(parentIndex);
    LayoutScratchState& parentScratch = layoutScratchByIndex[parentIndex];
    const UILogicalRect parentWorldRect = parentScratch.worldRect;
    const UILogicalRect unscrolledContentRect{
        .x = normalizeFloat(parentWorldRect.x + parentStyle.padding.left),
        .y = normalizeFloat(parentWorldRect.y + parentStyle.padding.top),
        .width = normalizeFloat((std::max)(0.0F, parentWorldRect.width - horizontalMargin(parentStyle.padding))),
        .height = normalizeFloat((std::max)(0.0F, parentWorldRect.height - verticalMargin(parentStyle.padding))),
    };
    UILogicalRect layoutContentRect = unscrolledContentRect;
    UILogicalRect descendantClip = parentScratch.descendantClip;
    if (parentRecord->kind == BuiltinElementKind::Popup ||
        parentRecord->kind == BuiltinElementKind::Menu ||
        parentRecord->kind == BuiltinElementKind::Tooltip ||
        parentStyle.clipDescendants)
    {
        descendantClip = intersectRects(descendantClip, parentScratch.worldRect);
    }

    if (parentScratch.effectiveVisibility == UIVisibility::Collapsed)
    {
        if (parentRecord->kind == BuiltinElementKind::ScrollView &&
            parentIndex < scrollViewLayoutScratchByNodeIndex.size())
        {
            scrollViewLayoutScratchByNodeIndex[parentIndex] = {};
        }
        if (parentRecord->kind == BuiltinElementKind::Popup && parentIndex < popupLayoutScratchByNodeIndex.size())
        {
            popupLayoutScratchByNodeIndex[parentIndex] = {};
        }
        const UINodeId parentNode = idForIndex(parentIndex);
        if (parentRecord->kind == BuiltinElementKind::Tooltip &&
            tooltipStorage.containsTooltip(parentNode))
        {
            tooltipStorage.layoutScratchByIndex(parentIndex) = {};
        }
        if (parentRecord->kind == BuiltinElementKind::Menu &&
            menuStorage.containsMenu(parentNode))
        {
            menuStorage.layoutScratchByIndex(parentIndex) = {};
        }
        if (parentRecord->kind == BuiltinElementKind::SplitView &&
            splitViewStorage.containsSplitView(parentNode))
        {
            splitViewStorage.layoutScratchByIndex(parentIndex) = {};
        }
        if (parentRecord->kind == BuiltinElementKind::TabView &&
            tabViewStorage.containsTabView(parentNode))
        {
            tabViewStorage.layoutScratchByIndex(parentIndex) = {};
        }
        if (parentRecord->kind == BuiltinElementKind::ListView && parentIndex < listViewLayoutScratchByNodeIndex.size())
        {
            listViewLayoutScratchByNodeIndex[parentIndex] = {};
            collapseListViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
            return Core::success();
        }
        if (parentRecord->kind == BuiltinElementKind::VirtualGridView)
        {
            collapseVirtualGridViewItems(
                idForIndex(parentIndex), unscrolledContentRect,
                parentWorldRect, descendantClip);
            return Core::success();
        }
        if (parentRecord->kind == BuiltinElementKind::DataGrid)
        {
            collapseDataGrid(
                idForIndex(parentIndex), unscrolledContentRect,
                parentWorldRect, descendantClip);
            return Core::success();
        }
        if (parentRecord->kind == BuiltinElementKind::DataGridRow)
        {
            return Core::success();
        }
        if (parentRecord->kind == BuiltinElementKind::TreeView && parentIndex < treeViewLayoutScratchByNodeIndex.size())
        {
            treeViewLayoutScratchByNodeIndex[parentIndex] = {};
            collapseTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
            return Core::success();
        }
        u32 collapsedChild = parentRecord->firstChildIndex;
        while (collapsedChild != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(collapsedChild);
            if (childRecord == nullptr)
            {
                break;
            }
            assignLayoutRect(collapsedChild, unscrolledContentRect, parentWorldRect, descendantClip);
            collapsedChild = childRecord->nextSiblingIndex;
        }
        return Core::success();
    }

    if (parentRecord->kind == BuiltinElementKind::ListView && parentIndex < listViewStatesByNodeIndex.size() &&
        parentIndex < listViewLayoutScratchByNodeIndex.size())
    {
        return arrangeListViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                    intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
    }
    if (parentRecord->kind == BuiltinElementKind::VirtualGridView)
    {
        return arrangeVirtualGridViewItems(
            idForIndex(parentIndex), unscrolledContentRect,
            parentWorldRect,
            intersectRects(parentScratch.descendantClip,
                           parentScratch.worldRect));
    }
    if (parentRecord->kind == BuiltinElementKind::DataGrid)
    {
        return arrangeDataGrid(
            idForIndex(parentIndex), unscrolledContentRect,
            parentWorldRect,
            intersectRects(parentScratch.descendantClip,
                           parentScratch.worldRect));
    }
    if (parentRecord->kind == BuiltinElementKind::DataGridRow)
    {
        // The owning DataGrid resolves every row-major cell rect in one
        // fixed-pool pass; ordinary child layout must not overwrite it.
        return Core::success();
    }
    if (parentRecord->kind == BuiltinElementKind::TreeView && parentIndex < treeViewStatesByNodeIndex.size() &&
        parentIndex < treeViewLayoutScratchByNodeIndex.size())
    {
        return arrangeTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                    intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
    }
    if (parentRecord->kind == BuiltinElementKind::TabView)
    {
        const UINodeId tabView = idForIndex(parentIndex);
        const TabViewState* state = tabViewStorage.tryTabView(tabView);
        if (state == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI TabView is missing retained state");
        }
        tabViewStorage.layoutScratchByIndex(parentIndex).metrics = UITabViewMetrics{
            .placement = state->config.placement,
        };
        // An unlinked TabView falls through to ordinary child layout with
        // cleared candidate metrics.
        if (tabViewStorage.relationshipValid(tabView))
        {
            const bool horizontal = state->config.placement == UITabViewPlacement::Top ||
                                    state->config.placement == UITabViewPlacement::Bottom;
            float stripExtent = 0.0F;
            const u32 itemCount = tabViewStorage.itemCount(tabView);
            for (u32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
            {
                const UITabViewItem item = tabViewStorage.itemAt(tabView, itemIndex);
                if (!item.hasValue())
                {
                    continue;
                }
                const UILogicalSize measured = layoutScratchByIndex[item.tab.index()].measuredSize;
                stripExtent = (std::max)(stripExtent, horizontal ? measured.height : measured.width);
            }
            const UITabViewRegions regions = resolveTabViewRegions(
                unscrolledContentRect, state->config.placement, stripExtent,
                state->config.contentGap);
            UILogicalRect stripCursor = regions.tabStripRect;
            for (u32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
            {
                const UITabViewItem item = tabViewStorage.itemAt(tabView, itemIndex);
                if (!item.hasValue())
                {
                    continue;
                }
                const UILogicalSize measured = layoutScratchByIndex[item.tab.index()].measuredSize;
                UILogicalRect tabRect = regions.tabStripRect;
                if (horizontal)
                {
                    tabRect.x = stripCursor.x;
                    tabRect.width = (std::max)(0.0F, measured.width);
                    stripCursor.x = normalizeFloat(stripCursor.x + tabRect.width + state->config.tabGap);
                }
                else
                {
                    tabRect.y = stripCursor.y;
                    tabRect.height = (std::max)(0.0F, measured.height);
                    stripCursor.y = normalizeFloat(stripCursor.y + tabRect.height + state->config.tabGap);
                }
                assignLayoutRect(item.tab.index(), tabRect, parentWorldRect, descendantClip);
            }
            const UINodeId activePanel = tabViewStorage.activePanel(tabView);
            if (activePanel.hasValue())
            {
                assignLayoutRect(activePanel.index(), regions.panelRect,
                                 parentWorldRect, descendantClip);
            }
            tabViewStorage.layoutScratchByIndex(parentIndex).metrics = UITabViewMetrics{
                .tabStripRect = regions.tabStripRect,
                .activePanelRect = regions.panelRect,
                .activeTab = tabViewStorage.activeTab(tabView),
                .activePanel = activePanel,
                .activeIndex = [&]() noexcept {
                    const TabState* active = tabViewStorage.tryTab(tabViewStorage.activeTab(tabView));
                    return active != nullptr ? active->ordinal : 0U;
                }(),
                .itemCount = itemCount,
                .placement = state->config.placement,
            };
            return Core::success();
        }
    }
    if (parentRecord->kind == BuiltinElementKind::SplitView)
    {
        const UINodeId splitView = idForIndex(parentIndex);
        const SplitViewState* state = splitViewStorage.trySplitView(splitView);
        if (state == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI SplitView is missing retained state");
        }
        const UISplitViewParts parts = splitViewStorage.parts(splitView);
        if (parts.hasValue())
        {
            // A SplitView owns the geometry of its three direct parts, so a
            // Collapsed pane/splitter must be reflected in the resolved
            // contract rather than only in paint/hit filtering. Hidden
            // parts keep their extent; Collapsed parts release it.
            UISplitViewConfig layoutConfig = resolvedSplitViewConfig(state->config);
            float requestedFraction = state->requestedFraction;
            bool primaryCollapsed = false;
            bool secondaryCollapsed = false;
            if (parts.primaryPane.index() < layoutScratchByIndex.size() &&
                layoutScratchByIndex[parts.primaryPane.index()].effectiveVisibility ==
                    UIVisibility::Collapsed)
            {
                primaryCollapsed = true;
                layoutConfig.minPrimarySize = 0.0F;
            }
            if (parts.secondaryPane.index() < layoutScratchByIndex.size() &&
                layoutScratchByIndex[parts.secondaryPane.index()].effectiveVisibility ==
                    UIVisibility::Collapsed)
            {
                secondaryCollapsed = true;
                layoutConfig.minSecondarySize = 0.0F;
            }
            if (primaryCollapsed && !secondaryCollapsed)
            {
                requestedFraction = 0.0F;
            }
            else if (secondaryCollapsed)
            {
                requestedFraction = 1.0F;
            }
            if (parts.splitter.index() < layoutScratchByIndex.size() &&
                layoutScratchByIndex[parts.splitter.index()].effectiveVisibility ==
                    UIVisibility::Collapsed)
            {
                layoutConfig.splitterExtent = 0.0F;
            }
            const auto plan = resolveSplitViewLayout(
                unscrolledContentRect, layoutConfig, requestedFraction);
            splitViewStorage.layoutScratchByIndex(parentIndex).metrics = plan.metrics;
            assignLayoutRect(parts.primaryPane.index(), plan.metrics.primaryRect,
                             parentWorldRect, descendantClip);
            assignLayoutRect(parts.splitter.index(), plan.metrics.splitterRect,
                             parentWorldRect, descendantClip);
            assignLayoutRect(parts.secondaryPane.index(), plan.metrics.secondaryRect,
                             parentWorldRect, descendantClip);
            return Core::success();
        }
        splitViewStorage.layoutScratchByIndex(parentIndex).metrics = UISplitViewMetrics{
            .fraction = state->requestedFraction,
            .orientation = state->config.orientation,
        };
    }

    const bool row = parentStyle.flexContainer.direction == UIFlexDirection::Row;
    const float configuredGap = row ? parentStyle.flexContainer.gap.column : parentStyle.flexContainer.gap.row;
    const float configuredCrossGap = row ? parentStyle.flexContainer.gap.row : parentStyle.flexContainer.gap.column;

    FlexWrapMeasurement flexMeasurement{};
    GridMeasurement gridMeasurement =
        beginGridMeasurement(parentStyle.gridContainer);
    const float initialContentMain = row ? unscrolledContentRect.width : unscrolledContentRect.height;
    u32 childIndex = parentRecord->firstChildIndex;
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        const UILayoutStyle childStyle = resolvedLayoutStyle(childIndex);
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
            childStyle.placement == UILayoutPlacement::Flow)
        {
            refreshMeasuredSizeForParentContent(childIndex, unscrolledContentRect, statistics);
            if (parentStyle.containerLayout == UIContainerLayout::Grid)
            {
                (void)appendGridMeasuredChild(
                    gridMeasurement, parentStyle.gridContainer,
                    childStyle, childScratch.measuredSize);
            }
            else
            {
                appendFlexMeasuredItem(
                    flexMeasurement,
                    parentStyle.flexContainer.direction,
                    parentStyle.flexContainer.wrap,
                    initialContentMain,
                    configuredGap,
                    configuredCrossGap,
                    childStyle,
                    childScratch,
                    statistics);
            }
        }
        childIndex = childRecord->nextSiblingIndex;
    }
    finishFlexMeasurement(flexMeasurement, configuredCrossGap);

    if ((parentStyle.containerLayout == UIContainerLayout::Grid &&
         !isValidGridMeasurement(
             gridMeasurement, parentStyle.gridContainer)) ||
        (parentStyle.containerLayout == UIContainerLayout::Flex &&
         !isValidFlexWrapMeasurement(flexMeasurement)))
    {
        return fail(
            UIErrorCode::InvalidLayout,
            "UI container layout accumulation exceeded capacity or produced a non-finite value");
    }

    if (parentRecord->kind == BuiltinElementKind::ScrollView && parentIndex < scrollViewPaintsByNodeIndex.size() &&
        parentIndex < scrollViewLayoutScratchByNodeIndex.size())
    {
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(parentIndex);
        if (state == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI ScrollView is missing Scroll behavior state");
        }
        const UIScrollViewPaint& paint = scrollViewPaintsByNodeIndex[parentIndex];
        const auto plan = resolveScrollViewLayout(ScrollViewLayoutInput{
            .availableRect = unscrolledContentRect,
            .rawContentSize =
                parentStyle.containerLayout == UIContainerLayout::Grid
                    ? gridMeasuredContentSize(
                          gridMeasurement, parentStyle.gridContainer)
                    : flexMeasurement.contentSize(
                          parentStyle.flexContainer.direction),
            .style = state->style,
            .scrollBarThickness = paint.thickness,
            .requestedOffset = state->requestedOffset,
        });
        scrollViewLayoutScratchByNodeIndex[parentIndex] = ScrollViewLayoutScratch{
            .metrics = plan.metrics,
            .viewportRect = plan.viewportRect,
        };
        layoutContentRect = plan.contentRect;
        descendantClip = intersectRects(descendantClip, plan.viewportRect);
    }

    parentScratch.hasArrangedFlexWrapConstraint =
        parentStyle.containerLayout == UIContainerLayout::Flex &&
        parentStyle.flexContainer.wrap == UIFlexWrap::Wrap &&
        flexMeasurement.itemCount != 0U;
    if (parentScratch.hasArrangedFlexWrapConstraint)
    {
        parentScratch.arrangedFlexWrapDirection =
            parentStyle.flexContainer.direction;
        parentScratch.arrangedFlexWrapMain =
            row ? layoutContentRect.width : layoutContentRect.height;
    }

    auto gridPlan = resolveGridLayout(
        parentStyle.gridContainer, layoutContentRect, gridMeasurement);
    if (parentStyle.containerLayout == UIContainerLayout::Grid &&
        !gridPlan.valid)
    {
        return fail(UIErrorCode::InvalidLayout,
                    "UI Grid track resolution produced invalid geometry");
    }

    const float arrangedContentMain = row
                                          ? layoutContentRect.width
                                          : layoutContentRect.height;
    FlexWrapMeasurement arrangedFlexMeasurement{};
    if (parentStyle.containerLayout == UIContainerLayout::Flex &&
        parentStyle.flexContainer.wrap == UIFlexWrap::Wrap)
    {
        u32 arrangedChildIndex = parentRecord->firstChildIndex;
        while (arrangedChildIndex != InvalidNodeIndex)
        {
            const NodeRecord* arrangedChildRecord =
                recordByIndex(arrangedChildIndex);
            if (arrangedChildRecord == nullptr)
            {
                break;
            }
            const UILayoutStyle& arrangedChildStyle =
                resolvedLayoutStyle(arrangedChildIndex);
            const LayoutScratchState& arrangedChildScratch =
                layoutScratchByIndex[arrangedChildIndex];
            if (arrangedChildScratch.effectiveVisibility !=
                    UIVisibility::Collapsed &&
                arrangedChildStyle.placement == UILayoutPlacement::Flow)
            {
                appendFlexMeasuredItem(
                    arrangedFlexMeasurement,
                    parentStyle.flexContainer.direction,
                    parentStyle.flexContainer.wrap, arrangedContentMain,
                    configuredGap, configuredCrossGap, arrangedChildStyle,
                    arrangedChildScratch, statistics);
            }
            arrangedChildIndex = arrangedChildRecord->nextSiblingIndex;
        }
        finishFlexMeasurement(
            arrangedFlexMeasurement, configuredCrossGap);
        if (!isValidFlexWrapMeasurement(arrangedFlexMeasurement))
        {
            return fail(
                UIErrorCode::InvalidLayout,
                "UI arranged Flex line accumulation produced invalid geometry");
        }
    }
    const float arrangedContentCross =
        row ? layoutContentRect.height : layoutContentRect.width;
    const Detail::FlexContentPlan flexContentPlan =
        Detail::resolveFlexContentPlan(
            parentStyle.flexContainer.alignContent, arrangedContentCross,
            configuredCrossGap, arrangedFlexMeasurement);
    const auto summarizeFlexLine = [&](u32 first) noexcept {
        FlexLineSummary summary{};
        u32 candidate = first;
        while (candidate != InvalidNodeIndex)
        {
            const NodeRecord* candidateRecord = recordByIndex(candidate);
            if (candidateRecord == nullptr)
            {
                break;
            }
            const UILayoutStyle& candidateStyle =
                resolvedLayoutStyle(candidate);
            const LayoutScratchState& candidateScratch =
                layoutScratchByIndex[candidate];
            if (candidateScratch.effectiveVisibility !=
                    UIVisibility::Collapsed &&
                candidateStyle.placement == UILayoutPlacement::Flow)
            {
                const Detail::FlexItemMeasurement item =
                    Detail::measureFlexItem(
                        parentStyle.flexContainer.direction,
                        arrangedContentMain, candidateStyle,
                        candidateScratch, statistics);
                if (Detail::flexItemStartsNewLine(
                        summary, parentStyle.flexContainer.wrap,
                        arrangedContentMain, configuredGap, item))
                {
                    break;
                }
                appendFlexLineItem(
                    summary, parentStyle.flexContainer.direction,
                    configuredGap, arrangedContentMain, candidateStyle,
                    candidateScratch, statistics);
            }
            candidate = candidateRecord->nextSiblingIndex;
        }
        return summary;
    };
    FlexLineSummary activeFlexLine{};
    FlexLinePlan flexPlan{};
    usize remainingFlexItems = 0U;
    float flexCrossOffset = flexContentPlan.nextCrossOffset;

    childIndex = parentRecord->firstChildIndex;
    while (childIndex != InvalidNodeIndex)
    {
        const NodeRecord* childRecord = recordByIndex(childIndex);
        if (childRecord == nullptr)
        {
            break;
        }
        const u32 currentChild = childIndex;
        childIndex = childRecord->nextSiblingIndex;
        const UILayoutStyle childStyle = resolvedLayoutStyle(currentChild);
        LayoutScratchState& childScratch = layoutScratchByIndex[currentChild];

        if (childScratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            assignLayoutRect(currentChild, layoutContentRect, parentWorldRect, descendantClip);
            if (childRecord->kind == BuiltinElementKind::Menu &&
                menuStorage.containsMenu(idForIndex(currentChild)))
            {
                menuStorage.layoutScratchByIndex(currentChild) = {};
            }
            continue;
        }
        if (childStyle.placement == UILayoutPlacement::Overlay)
        {
            if (childRecord->kind == BuiltinElementKind::Popup && currentChild < popupStatesByNodeIndex.size() &&
                currentChild < popupLayoutScratchByNodeIndex.size())
            {
                arrangePopupChild(currentChild, parentWorldRect, viewportRect, statistics);
            } else if (childRecord->kind == BuiltinElementKind::Tooltip &&
                       tooltipStorage.containsTooltip(idForIndex(currentChild)))
            {
                arrangeTooltipChild(currentChild, parentWorldRect, viewportRect, statistics);
            } else if (childRecord->kind == BuiltinElementKind::Menu &&
                       menuStorage.containsMenu(idForIndex(currentChild)))
            {
                arrangeMenuChild(currentChild, parentWorldRect, viewportRect, statistics);
            } else
            {
                arrangeOverlayChild(currentChild, layoutContentRect, parentWorldRect, descendantClip, statistics);
            }
            continue;
        }

        if (parentStyle.containerLayout == UIContainerLayout::Grid)
        {
            const auto area = resolveGridArea(
                gridPlan.placement, childStyle.gridItem);
            if (!area.valid)
            {
                return fail(
                    UIErrorCode::InvalidLayout,
                    "UI Grid automatic placement exceeded the fixed 8x8 capacity");
            }
            const UILogicalRect cell = gridAreaRect(gridPlan, area);
            refreshMeasuredSizeForParentContent(
                currentChild, cell, statistics);
            assignLayoutRect(
                currentChild,
                resolveGridItemRectInArea(
                    gridPlan, area, parentStyle.gridContainer,
                    childStyle, childScratch, statistics),
                parentWorldRect, descendantClip);
        }
        else
        {
            if (remainingFlexItems == 0U)
            {
                activeFlexLine = summarizeFlexLine(currentChild);
                if (!isValidFlexLineSummary(activeFlexLine) ||
                    activeFlexLine.itemCount == 0U)
                {
                    return fail(
                        UIErrorCode::InvalidLayout,
                        "UI Flex line resolution produced invalid geometry");
                }
                UILogicalRect lineRect = layoutContentRect;
                if (parentStyle.flexContainer.wrap == UIFlexWrap::Wrap)
                {
                    if (row)
                    {
                        lineRect.y = normalizeFloat(
                            lineRect.y + flexCrossOffset);
                        lineRect.height = normalizeFloat(
                            activeFlexLine.totalCross +
                            flexContentPlan.lineCrossGrowth);
                    }
                    else
                    {
                        lineRect.x = normalizeFloat(
                            lineRect.x + flexCrossOffset);
                        lineRect.width = normalizeFloat(
                            activeFlexLine.totalCross +
                            flexContentPlan.lineCrossGrowth);
                    }
                }
                flexPlan = resolveFlexLinePlan(
                    parentStyle, lineRect, activeFlexLine);
                remainingFlexItems = activeFlexLine.itemCount;
            }
            assignLayoutRect(
                currentChild,
                resolveFlexItemRect(
                    flexPlan, childStyle, childScratch, statistics),
                parentWorldRect, descendantClip);
            --remainingFlexItems;
            if (remainingFlexItems == 0U &&
                parentStyle.flexContainer.wrap == UIFlexWrap::Wrap)
            {
                flexCrossOffset = normalizeFloat(
                    flexCrossOffset + activeFlexLine.totalCross +
                    flexContentPlan.lineCrossGrowth +
                    flexContentPlan.crossGap);
            }
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::arrangeLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                                         LayoutPassStatistics& statistics)
{
    const UILogicalRect viewportRect{
        .x = 0.0F,
        .y = 0.0F,
        .width = viewportSize.width,
        .height = viewportSize.height,
    };

    u32 ordinal = 0;
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            continue;
        }
        const u32 currentOrdinal = ordinal++;
        if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkArrange))
        {
            continue;
        }
        ++statistics.arrangedNodeCount;
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        if (record->parentIndex == InvalidNodeIndex)
        {
            assignLayoutRect(index,
                             UILogicalRect{
                                 .x = 0.0F,
                                 .y = 0.0F,
                                 .width = scratch.measuredSize.width,
                                 .height = scratch.measuredSize.height,
                             },
                             viewportRect, viewportRect);
        }
        scratch.layoutOrdinal = currentOrdinal;
        scratch.paintOrdinal = currentOrdinal;
        if (Core::Status arranged = arrangeChildren(index, viewportRect, statistics); !arranged)
        {
            return arranged;
        }
    }
    return Core::success();
}

[[nodiscard]] UICommittedContentPlacement UIContext::Impl::contentPlacementFor(u32 index) const noexcept
{
    const LayoutScratchState& scratch = layoutScratchByIndex[index];
    const UILayoutStyle layout = resolvedLayoutStyle(index);
    const NodeRecord* record = recordByIndex(index);
    float leadingReservedWidth = 0.0F;
    float trailingReservedWidth = 0.0F;
    if (record != nullptr && record->kind == BuiltinElementKind::Dropdown &&
        index < dropdownStatesByNodeIndex.size())
    {
        const UIDropdownPaint& dropdown = dropdownStatesByNodeIndex[index].paint;
        trailingReservedWidth =
            dropdown.indicatorWidth + dropdown.indicatorInset * 2.0F;
    }
    if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
        index < radioButtonStatesByNodeIndex.size() &&
        radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
    {
        const float indicatorExtent = (std::min)({
            radioButtonStatesByNodeIndex[index].paint.indicatorExtent,
            scratch.worldRect.width,
            scratch.worldRect.height,
        });
        leadingReservedWidth =
            indicatorExtent + radioButtonStatesByNodeIndex[index].paint.labelGap;
    }
    if (record != nullptr && record->kind == BuiltinElementKind::MenuItem)
    {
        const MenuItemState* item = menuStorage.tryItem(idForIndex(index));
        if (item != nullptr && item->config.kind != UIMenuItemKind::Separator)
        {
            leadingReservedWidth = productTheme.controls.menuItemIndicatorExtent +
                                   productTheme.controls.menuItemIndicatorGap;
            if (item->config.kind == UIMenuItemKind::Submenu)
            {
                trailingReservedWidth =
                    productTheme.controls.menuItemIndicatorExtent +
                    productTheme.controls.menuItemIndicatorGap;
            }
        }
    }

    const UIImageContent* image = imageContentStorage.get(index);
    const UITextMetrics* metrics =
        index < textStatesByIndex.size() ? presentationTextMetricsFor(index)
                                         : nullptr;
    UIContentAlignment alignment =
        metrics != nullptr ? textStatesByIndex[index].alignment
                           : image != nullptr ? image->alignment : UIContentAlignment{};
    const UILogicalSize* intrinsicSize =
        metrics != nullptr ? &metrics->measuredSize
                           : image != nullptr ? &image->source.intrinsicLogicalSize : nullptr;
    UILogicalSize emptyTextEditIntrinsicSize{};
    if (metrics == nullptr && record != nullptr &&
        record->kind == BuiltinElementKind::TextEdit &&
        index < textStatesByIndex.size())
    {
        const WidgetTextState& text = textStatesByIndex[index];
        alignment = text.alignment;
        emptyTextEditIntrinsicSize.height = normalizeFloat(
            (std::max)(0.0F, text.style.logicalSize * text.style.lineHeightScale));
        intrinsicSize = &emptyTextEditIntrinsicSize;
    }
    return resolveContentPlacement(
        scratch.worldRect, layout.padding, leadingReservedWidth,
        trailingReservedWidth, alignment, intrinsicSize);
}

[[nodiscard]] bool UIContext::Impl::hasVirtualGridPreview(u32 index) const noexcept
{
    const NodeRecord* record = recordByIndex(index);
    if (record == nullptr || record->kind != BuiltinElementKind::VirtualGridViewItem)
    {
        return false;
    }
    const UIImageContent* image = imageContentStorage.get(index);
    const WidgetTextState* text = index < textStatesByIndex.size()
                                      ? &textStatesByIndex[index]
                                      : nullptr;
    return image != nullptr && text != nullptr && text->hasContent;
}

[[nodiscard]] UICommittedContentPlacement UIContext::Impl::virtualGridImagePlacement(
    const UICommittedLayoutEntry& layoutEntry) const noexcept
{
    UICommittedContentPlacement placement = layoutEntry.contentPlacement;
    if (!hasVirtualGridPreview(layoutEntry.node.index()))
    {
        return placement;
    }
    if (layoutEntry.worldRect.height <= VirtualGridListItemHeightThreshold)
    {
        placement.contentBox.width = (std::min)(
            VirtualGridListPreviewExtent,
            (std::max)(0.0F, placement.contentBox.width));
        placement.origin = placement.contentBox.origin();
        return placement;
    }
    const float thumbnailHeight = (std::min)(
        VirtualGridPreviewMaxExtent,
        (std::max)(0.0F, placement.contentBox.height));
    placement.contentBox.height = thumbnailHeight;
    placement.origin = placement.contentBox.origin();
    return placement;
}

[[nodiscard]] UICommittedContentPlacement UIContext::Impl::virtualGridTextPlacement(
    const UICommittedLayoutEntry& layoutEntry) const noexcept
{
    UICommittedContentPlacement placement = layoutEntry.contentPlacement;
    if (!hasVirtualGridPreview(layoutEntry.node.index()))
    {
        return placement;
    }
    const u32 index = layoutEntry.node.index();
    if (layoutEntry.worldRect.height <= VirtualGridListItemHeightThreshold)
    {
        const float thumbnailWidth = (std::min)(
            VirtualGridListPreviewExtent,
            (std::max)(0.0F, placement.contentBox.width));
        placement.contentBox.x = normalizeFloat(
            placement.contentBox.x + thumbnailWidth + VirtualGridPreviewGap);
        placement.contentBox.width = (std::max)(
            0.0F,
            placement.contentBox.width - thumbnailWidth - VirtualGridPreviewGap);
    }
    else
    {
        const float thumbnailHeight = (std::min)(
            VirtualGridPreviewMaxExtent,
            (std::max)(0.0F, placement.contentBox.height));
        const float availableHeight = (std::max)(
            0.0F,
            placement.contentBox.height - thumbnailHeight - VirtualGridPreviewGap);
        placement.contentBox.y = normalizeFloat(
            placement.contentBox.y + thumbnailHeight + VirtualGridPreviewGap);
        placement.contentBox.height = availableHeight;
    }
    const UITextMetrics* metrics =
        index < textStatesByIndex.size() ? presentationTextMetricsFor(index) : nullptr;
    if (metrics == nullptr)
    {
        placement.origin = placement.contentBox.origin();
        placement.hasIntrinsicContent = false;
        placement.intrinsicSize = {};
        return placement;
    }
    const UIContentAlignment alignment = textStatesByIndex[index].alignment;
    const float horizontalFree = (std::max)(
        0.0F, placement.contentBox.width - metrics->measuredSize.width);
    const float verticalFree = (std::max)(
        0.0F, placement.contentBox.height - metrics->measuredSize.height);
    const auto alignedOffset = [](UIAxisAlignment axis, float freeSpace) noexcept {
        switch (axis)
        {
        case UIAxisAlignment::Center:
            return freeSpace * 0.5F;
        case UIAxisAlignment::End:
            return freeSpace;
        case UIAxisAlignment::Start:
        case UIAxisAlignment::Stretch:
            return 0.0F;
        }
        return 0.0F;
    };
    placement.origin = UILogicalPoint{
        .x = normalizeFloat(placement.contentBox.x +
                            alignedOffset(alignment.horizontal, horizontalFree)),
        .y = normalizeFloat(placement.contentBox.y +
                            alignedOffset(alignment.vertical, verticalFree)),
    };
    placement.intrinsicSize = metrics->measuredSize;
    placement.hasIntrinsicContent = true;
    return placement;
}

void UIContext::Impl::buildCommittedLayout(std::pmr::vector<UICommittedLayoutEntry>& output,
                          const std::pmr::vector<u32>& order) const noexcept
{
    output.clear();
    u32 paintOrdinal = 0;
    const auto appendNode = [&](u32 index) noexcept {
        const LayoutScratchState& scratch = layoutScratchByIndex[index];
        output.push_back(UICommittedLayoutEntry{
            .node = idForIndex(index),
            .localRect = scratch.localRect,
            .worldRect = scratch.worldRect,
            .effectiveClip = scratch.effectiveClip,
            .contentPlacement = contentPlacementFor(index),
            .effectiveVisibility = scratch.effectiveVisibility,
            .layoutOrdinal = scratch.layoutOrdinal,
            .paintOrdinal = paintOrdinal,
        });
        ++paintOrdinal;
    };
    const auto belongsToActiveMenuChain = [&](u32 index) noexcept {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            return false;
        }
        if (record->kind == BuiltinElementKind::Menu)
        {
            return menuStorage.isInActiveChain(idForIndex(index));
        }
        if (record->kind == BuiltinElementKind::MenuItem)
        {
            return menuStorage.isInActiveChain(
                menuStorage.menuForItem(idForIndex(index)));
        }
        return false;
    };
    const auto appendPass = [&](bool popupPass, bool tooltipPass,
                                bool deferActiveMenus = false) noexcept {
        for (const u32 index : order)
        {
            if (layoutScratchByIndex[index].inPopupSubtree != popupPass ||
                layoutScratchByIndex[index].inTooltipSubtree != tooltipPass ||
                (deferActiveMenus && belongsToActiveMenuChain(index)))
            {
                continue;
            }
            appendNode(index);
        }
    };
    appendPass(false, false);
    appendPass(true, false, true);

    UINodeId activeChainMenu = menuStorage.rootMenu();
    usize visited = 0;
    while (activeChainMenu.hasValue() &&
           visited++ < menuStorage.capacity())
    {
        appendNode(activeChainMenu.index());
        const NodeRecord* menuRecord =
            nodes.tryGet(activeChainMenu.storageId());
        u32 childIndex = menuRecord != nullptr
                             ? menuRecord->firstChildIndex
                             : InvalidNodeIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            appendNode(childIndex);
            childIndex = child->nextSiblingIndex;
        }
        activeChainMenu = menuStorage.activeChildMenu(activeChainMenu);
    }
    appendPass(false, true);
    appendPass(true, true);
}

[[nodiscard]] Core::Result<CommittedHitBuildResult>
UIContext::Impl::buildCommittedHit(std::pmr::vector<UICommittedHitEntry>& output,
                  std::span<const UICommittedLayoutEntry> layoutEntries)
{
    usize visibleEntryCount = 0;
    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        if (layoutEntry.effectiveVisibility == UIVisibility::Visible)
        {
            ++visibleEntryCount;
        }
    }
    if (visibleEntryCount > capacityConfig.hitSnapshotCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI committed hit snapshot capacity has been exhausted");
    }

    output.clear();
    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        hitEntryIndexByNodeIndex[layoutEntry.node.index()] = InvalidUIHitEntryIndex;
    }
    usize targetCount = 0;
    u32 activeModalEntryIndex = InvalidUIHitEntryIndex;

    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
        {
            continue;
        }
        if (!contains(layoutEntry.node))
        {
            return fail(UIErrorCode::InvalidNode, "UI hit snapshot layout references a stale node");
        }

        const u32 nodeIndex = layoutEntry.node.index();
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr)
        {
            return fail(UIErrorCode::InvalidNode, "UI hit snapshot node record is unavailable");
        }

        const UIPointerHitPolicy policy = pointerHitPoliciesByIndex[nodeIndex];
        if (!isValidPointerHitPolicy(policy))
        {
            return fail(UIErrorCode::InvalidPointerPolicy, "UI hit snapshot contains an invalid pointer policy");
        }
        const UIFocusScopeMode focusScopeMode = focusScopeModesByNodeIndex[nodeIndex];
        if (!isValidFocusScopeMode(focusScopeMode))
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI hit snapshot contains an invalid focus-scope mode");
        }

        const u32 entryIndex = static_cast<u32>(output.size());
        u32 parentEntryIndex = InvalidUIHitEntryIndex;
        u32 rootEntryIndex = entryIndex;
        u32 focusScopeEntryIndex = InvalidUIHitEntryIndex;
        u32 modalScopeEntryIndex = InvalidUIHitEntryIndex;
        if (record->parentIndex != InvalidNodeIndex)
        {
            parentEntryIndex = hitEntryIndexByNodeIndex[record->parentIndex];
            rootEntryIndex = hitEntryIndexByNodeIndex[record->rootIndex];
            if (parentEntryIndex == InvalidUIHitEntryIndex || rootEntryIndex == InvalidUIHitEntryIndex)
            {
                return fail(UIErrorCode::InvalidNode, "UI hit snapshot visible ancestry is incomplete");
            }
            focusScopeEntryIndex = output[parentEntryIndex].focusScopeEntryIndex;
            modalScopeEntryIndex = output[parentEntryIndex].modalScopeEntryIndex;
        }
        if (focusScopeMode == UIFocusScopeMode::Contain ||
            hasBehavior(record->behaviors, UIElementBehavior::ModalBarrier))
        {
            focusScopeEntryIndex = entryIndex;
        }
        if (hasBehavior(record->behaviors, UIElementBehavior::ModalBarrier))
        {
            modalScopeEntryIndex = entryIndex;
            activeModalEntryIndex = entryIndex;
        }

        output.push_back(UICommittedHitEntry{
            .node = layoutEntry.node,
            .parentEntryIndex = parentEntryIndex,
            .rootEntryIndex = rootEntryIndex,
            .focusScopeEntryIndex = focusScopeEntryIndex,
            .modalScopeEntryIndex = modalScopeEntryIndex,
            .worldRect = layoutEntry.worldRect,
            .effectiveClip = layoutEntry.effectiveClip,
            .paintOrdinal = layoutEntry.paintOrdinal,
            .policy = policy,
            .behaviors = record->behaviors,
        });
        hitEntryIndexByNodeIndex[nodeIndex] = entryIndex;
        if (policy == UIPointerHitPolicy::Targetable)
        {
            ++targetCount;
        }
    }
    return CommittedHitBuildResult{
        .targetCount = targetCount,
        .activeModalEntryIndex = activeModalEntryIndex,
    };
}

} // namespace Tina::UI
