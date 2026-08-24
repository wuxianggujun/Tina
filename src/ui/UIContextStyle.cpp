#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::widgetPaintColor(UINodeId node,
                                                         UIPremultipliedRgba8Color color) const noexcept
{
    constexpr u8 DisabledOpacity = 140;
    return isCandidateNodeEnabled(node) ? color : applyOpacity(color, DisabledOpacity);
}

[[nodiscard]] bool UIContext::Impl::isFocusVisible(UINodeId node) const noexcept
{
    return inputModality != UIInputModality::Pointer &&
           (defaultActionFocusButton == node || textInputFocus == node);
}

[[nodiscard]] UISplitViewConfig UIContext::Impl::resolvedSplitViewConfig(
    const UISplitViewConfig& authored) const noexcept
{
    UISplitViewConfig resolved = authored;
    if (!(resolved.splitterExtent > 0.0F))
    {
        resolved.splitterExtent = productTheme.controls.splitterHitExtent;
    }
    return resolved;
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolveBuiltinBoxFillColor(
    UINodeId node, u32 nodeIndex,
    UIPremultipliedRgba8Color normalColor) const noexcept
{
    UIPremultipliedRgba8Color color = normalColor;
    const NodeRecord* record = recordByIndex(nodeIndex);
    if (record != nullptr && record->kind == BuiltinElementKind::Checkbox &&
        nodeIndex < checkboxPaintsByNodeIndex.size())
    {
        const UICheckboxPaint& paint = checkboxPaintsByNodeIndex[nodeIndex];
        const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
        const bool checked = toggleValue != nullptr && *toggleValue != 0;
        const bool switchPresentation =
            paint.presentation == UIToggleIndicatorPresentation::Switch;
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (switchPresentation && checked)
        {
            applyOverride(paint.checkedBackgroundColor);
        }
        if (isFocusVisible(node))
        {
            applyOverride(switchPresentation && checked
                              ? paint.checkedFocusedBackgroundColor
                              : paint.focusedIndicatorColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(switchPresentation && checked
                              ? paint.checkedHoveredBackgroundColor
                              : paint.hoveredIndicatorColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(switchPresentation && checked
                              ? paint.checkedPressedBackgroundColor
                              : paint.pressedIndicatorColor);
        }
        return widgetPaintColor(node, color);
    }
    if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
        nodeIndex < radioButtonStatesByNodeIndex.size())
    {
        const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
        const UIRadioButtonPaint& paint = radio.paint;
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (radio.selected)
        {
            applyOverride(paint.selectedBackgroundColor);
        }
        if (isFocusVisible(node))
        {
            applyOverride(radio.selected ? paint.selectedFocusedBackgroundColor
                                         : paint.focusedBackgroundColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(radio.selected ? paint.selectedHoveredBackgroundColor
                                         : paint.hoveredBackgroundColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(radio.selected ? paint.selectedPressedBackgroundColor
                                         : paint.pressedBackgroundColor);
        }
        if (!isNodeEnabled(node))
        {
            applyOverride(paint.disabledBackgroundColor);
        }
        return widgetPaintColor(node, color);
    }
    if (record != nullptr && record->kind == BuiltinElementKind::Tab &&
        tabViewStorage.containsTab(node))
    {
        const UITabPaint& paint = tabViewStorage.tabPaintByIndex(nodeIndex);
        const UINodeId tabView = tabViewStorage.tabViewForTab(node);
        const bool selected = tabView.hasValue() && tabViewStorage.activeTab(tabView) == node;
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (selected)
        {
            applyOverride(paint.selectedBackgroundColor);
        }
        if (isFocusVisible(node))
        {
            applyOverride(selected ? paint.selectedFocusedBackgroundColor
                                   : paint.focusedBackgroundColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(selected ? paint.selectedHoveredBackgroundColor
                                   : paint.hoveredBackgroundColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(selected ? paint.selectedPressedBackgroundColor
                                   : paint.pressedBackgroundColor);
        }
        if (!isNodeEnabled(node))
        {
            applyOverride(paint.disabledBackgroundColor);
        }
        return widgetPaintColor(node, color);
    }
    if (record != nullptr && record->kind == BuiltinElementKind::TextEdit &&
        nodeIndex < textEditPaintsByNodeIndex.size())
    {
        const UITextEditPaint& paint = textEditPaintsByNodeIndex[nodeIndex];
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (textInputFocus == node && isLiveTextEdit(node) && isFocusVisible(node))
        {
            applyOverride(paint.focusedBackgroundColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(paint.hoveredBackgroundColor);
        }
        if (armedTextEdit == node)
        {
            applyOverride(paint.pressedBackgroundColor);
        }
        if (!isNodeEnabled(node))
        {
            applyOverride(paint.disabledBackgroundColor);
        }
        return widgetPaintColor(node, color);
    }
    if (record != nullptr && record->kind == BuiltinElementKind::Dropdown &&
        nodeIndex < dropdownStatesByNodeIndex.size())
    {
        const UINodeId popup = popupForDropdown(node);
        const bool open = popup.hasValue() && popup.index() < popupStatesByNodeIndex.size() &&
                          popupStatesByNodeIndex[popup.index()].open;
        const UIStraightSrgba8Color openColor =
            dropdownStatesByNodeIndex[nodeIndex].paint.openBackgroundColor;
        if (open && openColor.alpha != 0)
        {
            color = premultiply(openColor);
        }
    }
    if (record == nullptr || !isButtonChromeKind(record->kind) || nodeIndex >= buttonPaintsByNodeIndex.size())
    {
        return widgetPaintColor(node, color);
    }

    const UIButtonPaint& paint = buttonPaintsByNodeIndex[nodeIndex];
    const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
        if (overrideColor.alpha != 0)
        {
            color = premultiply(overrideColor);
        }
    };
    if (isFocusVisible(node))
    {
        applyOverride(paint.focusedBackgroundColor);
    }
    if (hoveredPrimaryControl == node)
    {
        applyOverride(paint.hoveredBackgroundColor);
    }
    if (isButtonPressed(node))
    {
        applyOverride(paint.pressedBackgroundColor);
    }
    if (!isNodeEnabled(node))
    {
        applyOverride(paint.disabledBackgroundColor);
    }
    return widgetPaintColor(node, color);
}

[[nodiscard]] u16 UIContext::Impl::boxFillOverrideMask(const NodeRecord& record) noexcept
{
    u16 relevantOverrides = static_cast<u16>(UIStyleOverride::BoxPaint);
    if (record.kind == BuiltinElementKind::Checkbox)
    {
        relevantOverrides |= static_cast<u16>(UIStyleOverride::CheckboxPaint);
    }
    else if (record.kind == BuiltinElementKind::RadioButton)
    {
        relevantOverrides |= static_cast<u16>(UIStyleOverride::RadioButtonPaint);
    }
    else if (record.kind == BuiltinElementKind::TextEdit)
    {
        relevantOverrides |= static_cast<u16>(UIStyleOverride::TextEditPaint);
    }
    else if (record.kind == BuiltinElementKind::Tab)
    {
        relevantOverrides |= static_cast<u16>(UIStyleOverride::TabPaint);
    }
    else if (isButtonChromeKind(record.kind))
    {
        relevantOverrides |= static_cast<u16>(UIStyleOverride::ButtonPaint);
    }
    return relevantOverrides;
}

[[nodiscard]] bool UIContext::Impl::hasLocalBoxFillOverride(u32 nodeIndex,
                                            const NodeRecord& record) const noexcept
{
    return nodeIndex < styleOverridesByNodeIndex.size() &&
           (styleOverridesByNodeIndex[nodeIndex] & boxFillOverrideMask(record)) != 0;
}

[[nodiscard]] UIStyleState UIContext::Impl::deriveStyleState(UINodeId node,
                                             u32 nodeIndex) const noexcept
{
    UIStyleState states = UIStyleState::None;
    if (hoveredPrimaryControl == node)
    {
        states |= UIStyleState::Hovered;
    }
    if (isButtonPressed(node) || armedTextEdit == node)
    {
        states |= UIStyleState::Pressed;
    }
    if (defaultActionFocusButton == node || textInputFocus == node)
    {
        states |= UIStyleState::Focused;
        if (isFocusVisible(node))
        {
            states |= UIStyleState::FocusVisible;
        }
    }
    if (!isCandidateNodeEnabled(node))
    {
        states |= UIStyleState::Disabled;
    }
    if (const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
        toggleValue != nullptr && *toggleValue != 0)
    {
        states |= UIStyleState::Checked;
    }

    const NodeRecord* record = recordByIndex(nodeIndex);
    if (record == nullptr)
    {
        return states;
    }
    if (record->kind == BuiltinElementKind::MenuItem &&
        menuStorage.itemChecked(node))
    {
        states |= UIStyleState::Checked;
    }
    if ((record->kind == BuiltinElementKind::RadioButton &&
         nodeIndex < radioButtonStatesByNodeIndex.size() &&
         radioButtonStatesByNodeIndex[nodeIndex].selected) ||
        (record->kind == BuiltinElementKind::DropdownItem &&
         isSelectedDropdownItem(node)) ||
        (record->kind == BuiltinElementKind::ListViewItem &&
         isSelectedListViewItem(node)) ||
        (record->kind == BuiltinElementKind::VirtualGridViewItem &&
         isSelectedVirtualGridViewItem(node)) ||
        (record->kind == BuiltinElementKind::TreeViewItem &&
         isSelectedTreeViewItem(node)) ||
        (record->kind == BuiltinElementKind::Tab &&
         tabViewStorage.tabViewForTab(node).hasValue() &&
         tabViewStorage.activeTab(tabViewStorage.tabViewForTab(node)) == node))
    {
        states |= UIStyleState::Selected;
    }
    bool open = record->kind == BuiltinElementKind::Popup &&
                nodeIndex < popupStatesByNodeIndex.size() &&
                popupStatesByNodeIndex[nodeIndex].open;
    if (record->kind == BuiltinElementKind::Menu)
    {
        open = menuStorage.isOpen(node);
    }
    if (record->kind == BuiltinElementKind::Dropdown)
    {
        const UINodeId popup = popupForDropdown(node);
        open = popup.hasValue() && popup.index() < popupStatesByNodeIndex.size() &&
               popupStatesByNodeIndex[popup.index()].open;
    }
    if (open)
    {
        states |= UIStyleState::Open;
    }
    if (armedSlider == node || armedScrollView == node)
    {
        states |= UIStyleState::Dragging;
    }
    return states;
}

[[nodiscard]] UIContext::Impl::StyleInteractionNodeSet UIContext::Impl::currentStyleInteractionNodes() const noexcept
{
    StyleInteractionNodeSet result{};
    result.add(hoveredPrimaryControl);
    result.add(armedPrimaryButton);
    result.add(defaultActionFocusButton);
    result.add(textInputFocus);
    result.add(armedSlider);
    result.add(armedScrollView);
    result.add(armedTextEdit);
    for (const UINodeId target : defaultActionPressState.pressedTargets())
    {
        result.add(target);
    }
    return result;
}

[[nodiscard]] Detail::UIStyleBoxFillResolution UIContext::Impl::resolveStyleBoxFill(
    u32 nodeIndex, UIStyleState states) const noexcept
{
    const usize classCount = styleClassCountsByNodeIndex[nodeIndex];
    const auto classes = std::span<const UIStyleClassId>(
        styleClassesByNodeIndex[nodeIndex].data(), classCount);
    return styleSheetStorage.resolveValidated(
        styleRolesByNodeIndex[nodeIndex], classes, states);
}

[[nodiscard]] std::span<const UIStyleClassId> UIContext::Impl::styleClassesFor(u32 nodeIndex) const noexcept
{
    return std::span<const UIStyleClassId>(styleClassesByNodeIndex[nodeIndex].data(),
                                           styleClassCountsByNodeIndex[nodeIndex]);
}

[[nodiscard]] bool UIContext::Impl::styleBackgroundTransitionEnabled() const noexcept
{
    return styleBackgroundColorTransitionSpec.duration.count() > 0.0;
}

[[nodiscard]] bool UIContext::Impl::needsStyleBackgroundMotionReservation(UIStyleRoleId role,
                                                         std::span<const UIStyleClassId> classes) const noexcept
{
    return styleBackgroundTransitionEnabled() &&
           styleSheetStorage.hasStatefulBoxFillCandidateValidated(role, classes);
}

void UIContext::Impl::unlinkTokenDependencyList(u32 nodeIndex, std::pmr::vector<UIStyleTokenId>& tokenByNode,
                               std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                               std::pmr::vector<u32>& headByToken) noexcept
{
    if (nodeIndex >= tokenByNode.size())
    {
        return;
    }
    const UIStyleTokenId token = tokenByNode[nodeIndex];
    if (!token.hasValue())
    {
        return;
    }
    const usize tokenSlot = token.value - 1U;
    if (tokenSlot >= headByToken.size())
    {
        tokenByNode[nodeIndex] = {};
        nextByNode[nodeIndex] = 0;
        prevByNode[nodeIndex] = 0;
        return;
    }
    const u32 next = nextByNode[nodeIndex];
    const u32 prev = prevByNode[nodeIndex];
    if (prev == 0)
    {
        headByToken[tokenSlot] = next;
    }
    else
    {
        nextByNode[prev - 1U] = next;
    }
    if (next != 0)
    {
        prevByNode[next - 1U] = prev;
    }
    nextByNode[nodeIndex] = 0;
    prevByNode[nodeIndex] = 0;
    tokenByNode[nodeIndex] = {};
}

void UIContext::Impl::linkTokenDependencyList(u32 nodeIndex, UIStyleTokenId token,
                             std::pmr::vector<UIStyleTokenId>& tokenByNode,
                             std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                             std::pmr::vector<u32>& headByToken) noexcept
{
    if (!token.hasValue() || nodeIndex >= tokenByNode.size())
    {
        return;
    }
    const usize tokenSlot = token.value - 1U;
    if (tokenSlot >= headByToken.size())
    {
        return;
    }
    const u32 nodeLink = nodeIndex + 1U;
    const u32 head = headByToken[tokenSlot];
    nextByNode[nodeIndex] = head;
    prevByNode[nodeIndex] = 0;
    if (head != 0)
    {
        prevByNode[head - 1U] = nodeLink;
    }
    headByToken[tokenSlot] = nodeLink;
    tokenByNode[nodeIndex] = token;
}

void UIContext::Impl::unlinkStyleTokenDependency(u32 nodeIndex) noexcept
{
    unlinkTokenDependencyList(nodeIndex, resolvedStyleColorTokenByNodeIndex,
                              styleTokenDependencyNextByNodeIndex,
                              styleTokenDependencyPrevByNodeIndex,
                              styleTokenDependencyHeadByTokenIndex);
}

void UIContext::Impl::linkStyleTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
{
    linkTokenDependencyList(nodeIndex, token, resolvedStyleColorTokenByNodeIndex,
                            styleTokenDependencyNextByNodeIndex,
                            styleTokenDependencyPrevByNodeIndex,
                            styleTokenDependencyHeadByTokenIndex);
}

void UIContext::Impl::setResolvedStyleColorTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
{
    if (nodeIndex >= resolvedStyleColorTokenByNodeIndex.size())
    {
        return;
    }
    if (resolvedStyleColorTokenByNodeIndex[nodeIndex] == token)
    {
        return;
    }
    unlinkStyleTokenDependency(nodeIndex);
    if (token.hasValue())
    {
        linkStyleTokenDependency(nodeIndex, token);
    }
}

void UIContext::Impl::unlinkImageTintTokenDependency(u32 nodeIndex) noexcept
{
    unlinkTokenDependencyList(nodeIndex, resolvedImageTintTokenByNodeIndex,
                              imageTintTokenDependencyNextByNodeIndex,
                              imageTintTokenDependencyPrevByNodeIndex,
                              imageTintTokenDependencyHeadByTokenIndex);
}

void UIContext::Impl::linkImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
{
    linkTokenDependencyList(nodeIndex, token, resolvedImageTintTokenByNodeIndex,
                            imageTintTokenDependencyNextByNodeIndex,
                            imageTintTokenDependencyPrevByNodeIndex,
                            imageTintTokenDependencyHeadByTokenIndex);
}

void UIContext::Impl::setResolvedImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
{
    if (nodeIndex >= resolvedImageTintTokenByNodeIndex.size())
    {
        return;
    }
    if (resolvedImageTintTokenByNodeIndex[nodeIndex] == token)
    {
        return;
    }
    unlinkImageTintTokenDependency(nodeIndex);
    if (token.hasValue())
    {
        linkImageTintTokenDependency(nodeIndex, token);
    }
}

[[nodiscard]] bool UIContext::Impl::hasLocalImageTintOverride(u32 nodeIndex) const noexcept
{
    return nodeIndex < styleOverridesByNodeIndex.size() &&
           (styleOverridesByNodeIndex[nodeIndex] &
            static_cast<u16>(UIStyleOverride::ImageTint)) != 0;
}

[[nodiscard]] bool UIContext::Impl::isStatefulControlImageTintRole(
    UIStyleRoleId role) noexcept
{
    return role == UIStyleRoleId::ButtonPrimary ||
           role == UIStyleRoleId::ButtonDanger ||
           role == UIStyleRoleId::ButtonTonal ||
           role == UIStyleRoleId::ButtonOutlined ||
           role == UIStyleRoleId::ButtonText ||
           role == UIStyleRoleId::SegmentedButton;
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::resolveBuiltinControlImageTint(
    UINodeId node, u32 nodeIndex, UIStraightSrgba8Color authoredTint) const noexcept
{
    if (nodeIndex >= styleRolesByNodeIndex.size())
    {
        return authoredTint;
    }
    const UIStyleRoleId role = styleRolesByNodeIndex[nodeIndex];
    if (!isStatefulControlImageTintRole(role))
    {
        return authoredTint;
    }

    UIStraightSrgba8Color tint = authoredTint;
    switch (role)
    {
    case UIStyleRoleId::ButtonPrimary:
        tint = productTheme.colors.onPrimary;
        break;
    case UIStyleRoleId::ButtonDanger:
        tint = productTheme.colors.onError;
        break;
    case UIStyleRoleId::ButtonTonal:
    case UIStyleRoleId::ButtonOutlined:
        tint = isButtonPressed(node) ? productTheme.colors.primary
                                     : productTheme.colors.onSurface;
        break;
    case UIStyleRoleId::ButtonText:
        tint = isButtonPressed(node)
                   ? productTheme.colors.primary
                   : ((hoveredPrimaryControl == node || isFocusVisible(node))
                          ? productTheme.colors.onSurface
                          : productTheme.colors.onSurfaceVariant);
        break;
    case UIStyleRoleId::SegmentedButton: {
        const bool selected =
            nodeIndex < radioButtonStatesByNodeIndex.size() &&
            radioButtonStatesByNodeIndex[nodeIndex].selected;
        tint = selected
                   ? productTheme.colors.onPrimaryContainer
                   : (isButtonPressed(node)
                          ? productTheme.colors.primary
                          : ((hoveredPrimaryControl == node || isFocusVisible(node))
                                 ? productTheme.colors.onSurface
                                 : productTheme.colors.onSurfaceVariant));
        break;
    }
    default:
        break;
    }
    if (!isCandidateNodeEnabled(node))
    {
        tint = scaleColorAlpha(tint, productTheme.states.disabledContentAlpha);
    }
    return tint;
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::resolvedImageTintColor(u32 nodeIndex,
                                                           const UIImageContent& image) const noexcept
{
    if (hasLocalImageTintOverride(nodeIndex))
    {
        return image.tint;
    }
    if (nodeIndex < resolvedImageTintValidByNodeIndex.size() &&
        resolvedImageTintValidByNodeIndex[nodeIndex] != 0)
    {
        return resolvedImageTintCacheByNodeIndex[nodeIndex];
    }
    return image.tint;
}

[[nodiscard]] usize UIContext::Impl::refreshResolvedStyleCache(u32 nodeIndex,
                                               UIStyleState states) noexcept
{
    const NodeRecord* record = recordByIndex(nodeIndex);
    if (record == nullptr || nodeIndex >= styleStatesByNodeIndex.size() ||
        nodeIndex >= resolvedBoxFillCacheByNodeIndex.size())
    {
        return 0;
    }

    const UINodeId node = idForIndex(nodeIndex);
    const bool hadResolvedStyle = resolvedStyleInitializedByNodeIndex[nodeIndex] != 0;
    const UIStyleState previousStates = styleStatesByNodeIndex[nodeIndex];
    const UIPremultipliedRgba8Color previousFill = resolvedBoxFillCacheByNodeIndex[nodeIndex];
    styleStatesByNodeIndex[nodeIndex] = states;
    UIPremultipliedRgba8Color resolvedFill = resolveBuiltinBoxFillColor(
        node, nodeIndex, localSolidFillCacheByIndex[nodeIndex]);

    const Detail::UIStyleBoxFillResolution resolution =
        resolveStyleBoxFill(nodeIndex, states);

    if (hasLocalBoxFillOverride(nodeIndex, *record))
    {
        resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
        setResolvedStyleColorTokenDependency(nodeIndex, {});
    }
    else
    {
        if (resolution.color.has_value())
        {
            resolvedFill = premultiply(*resolution.color);
        }
        // Optional paint-only motion when interaction state changes the
        // stylesheet BoxFill color. Style binding has already reserved the
        // track, so activation failure is an internal invariant violation.
        const bool stateChanged = previousStates != states;
        const bool colorChanged = previousFill != resolvedFill;
        const bool canAnimate =
            hadResolvedStyle && stateChanged && colorChanged && !reducedMotionEnabled &&
            styleBackgroundTransitionEnabled() && motionTrackStorage.hasPersistentReservation(
                                                      node, UIAnimatableProperty::BackgroundColor);
        if (canAnimate)
        {
            const auto presentation = motionPresentationFor(node);
            const UIStraightSrgba8Color startColor = presentation.hasBackgroundColor
                                                         ? presentation.backgroundColor
                                                         : unpremultiplyColor(previousFill);
            const UIStraightSrgba8Color targetColor =
                resolution.color.has_value() ? *resolution.color : unpremultiplyColor(resolvedFill);
            if (Core::Status motionStatus = motionTrackStorage.activateReservedStyleColor(
                    node, startColor, targetColor, styleBackgroundColorTransitionSpec, motionNow());
                !motionStatus)
            {
                std::terminate();
            }
        }
        resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
        setResolvedStyleColorTokenDependency(nodeIndex, resolution.colorToken);
    }

    const UIImageContent* retainedImage = imageContentStorage.get(nodeIndex);
    const bool hasBuiltinControlTint =
        retainedImage != nullptr &&
        isStatefulControlImageTintRole(styleRolesByNodeIndex[nodeIndex]);
    if (hasLocalImageTintOverride(nodeIndex) ||
        (!resolution.imageTint.has_value() && !hasBuiltinControlTint))
    {
        if (nodeIndex < resolvedImageTintValidByNodeIndex.size())
        {
            resolvedImageTintValidByNodeIndex[nodeIndex] = 0;
            resolvedImageTintCacheByNodeIndex[nodeIndex] = {};
        }
        setResolvedImageTintTokenDependency(nodeIndex, {});
    }
    else
    {
        resolvedImageTintCacheByNodeIndex[nodeIndex] =
            resolution.imageTint.has_value()
                ? *resolution.imageTint
                : resolveBuiltinControlImageTint(node, nodeIndex,
                                                 retainedImage->tint);
        resolvedImageTintValidByNodeIndex[nodeIndex] = 1;
        setResolvedImageTintTokenDependency(nodeIndex, resolution.imageTintToken);
    }
    resolvedStyleInitializedByNodeIndex[nodeIndex] = 1;
    return resolution.candidateRuleCount;
}

[[nodiscard]] usize UIContext::Impl::refreshResolvedStyleCache(u32 nodeIndex) noexcept
{
    return refreshResolvedStyleCache(nodeIndex,
                                     deriveStyleState(idForIndex(nodeIndex), nodeIndex));
}

[[nodiscard]] UIBoxPaint UIContext::Impl::resolvedBoxChrome(UINodeId node, u32 nodeIndex) const noexcept
{
    // Motion presentation (border/radius/visual offset) is applied first;
    // button pressed/focus-visible chrome may still override for interaction feedback.
    UIBoxPaint chrome = presentationBoxPaint(node, nodeIndex);
    const NodeRecord* record = recordByIndex(nodeIndex);
    if (record == nullptr || !isCandidateNodeEnabled(node))
    {
        return chrome;
    }

    UIStraightSrgba8Color focusedBorderColor{};
    if (record->kind == BuiltinElementKind::Tab &&
        tabViewStorage.containsTab(node))
    {
        focusedBorderColor = tabViewStorage.tabPaintByIndex(nodeIndex).focusedBorderColor;
    }
    else if (isButtonChromeKind(record->kind) && nodeIndex < buttonPaintsByNodeIndex.size())
    {
        focusedBorderColor = buttonPaintsByNodeIndex[nodeIndex].focusedBorderColor;
    }
    else if (record->kind == BuiltinElementKind::RadioButton &&
             nodeIndex < radioButtonStatesByNodeIndex.size())
    {
        focusedBorderColor = radioButtonStatesByNodeIndex[nodeIndex].paint.focusedBorderColor;
    }
    else if (record->kind == BuiltinElementKind::TextEdit &&
             nodeIndex < textEditPaintsByNodeIndex.size())
    {
        focusedBorderColor = textEditPaintsByNodeIndex[nodeIndex].focusedBorderColor;
    }
    else
    {
        return chrome;
    }

    if (isButtonPressed(node))
    {
        std::swap(chrome.borderLight, chrome.borderDark);
        chrome.shadowOffsetX = 0.0F;
        chrome.shadowOffsetY = 0.0F;
        return chrome;
    }

    if (isFocusVisible(node) && focusedBorderColor.alpha != 0)
    {
        chrome.borderLight = focusedBorderColor;
        chrome.borderDark = focusedBorderColor;
        if (!(chrome.borderWidth > 0.0F))
        {
            chrome.borderWidth = 1.0F;
        }
    }
    return chrome;
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedRadioIndicatorColor(UINodeId node, u32 nodeIndex) const noexcept
{
    if (nodeIndex >= radioButtonStatesByNodeIndex.size())
    {
        return {};
    }

    const UIRadioButtonPaint& paint = radioButtonStatesByNodeIndex[nodeIndex].paint;
    UIPremultipliedRgba8Color color = premultiply(paint.indicatorColor);
    const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
        if (overrideColor.alpha != 0)
        {
            color = premultiply(overrideColor);
        }
    };
    if (isFocusVisible(node))
    {
        applyOverride(paint.focusedIndicatorColor);
    }
    if (hoveredPrimaryControl == node)
    {
        applyOverride(paint.hoveredIndicatorColor);
    }
    if (isButtonPressed(node))
    {
        applyOverride(paint.pressedIndicatorColor);
    }
    return widgetPaintColor(node, color);
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedDropdownSelectionColor(UINodeId item) const noexcept
{
    const UINodeId dropdown = dropdownForItem(item);
    const Detail::UISelectBehaviorState* select =
        dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
    if (!dropdown.hasValue() || dropdown.index() >= dropdownStatesByNodeIndex.size() ||
        select == nullptr || select->selectedOption != item)
    {
        return {};
    }
    return widgetPaintColor(item,
                            premultiply(dropdownStatesByNodeIndex[dropdown.index()].paint
                                            .selectedItemBackgroundColor));
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedCollectionSelectionColor(
    UINodeId item, UINodeId collection, UIStraightSrgba8Color normalColor,
    UIStraightSrgba8Color hoveredColor, UIStraightSrgba8Color focusedColor,
    UIStraightSrgba8Color pressedColor) const noexcept
{
    UIPremultipliedRgba8Color color = premultiply(normalColor);
    const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
        if (overrideColor.alpha != 0)
        {
            color = premultiply(overrideColor);
        }
    };
    if (isCandidateNodeEnabled(collection) && isCandidateNodeEnabled(item))
    {
        if (isFocusVisible(collection))
        {
            applyOverride(focusedColor);
        }
        if (hoveredPrimaryControl == item)
        {
            applyOverride(hoveredColor);
        }
        if (isButtonPressed(item))
        {
            applyOverride(pressedColor);
        }
    }
    return widgetPaintColor(item, color);
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedListViewSelectionColor(UINodeId item) const noexcept
{
    const UINodeId listView = listViewForItem(item);
    if (!listView.hasValue() || listView.index() >= listViewStatesByNodeIndex.size() ||
        !isSelectedListViewItem(item))
    {
        return {};
    }
    const UIListViewPaint& paint = listViewStatesByNodeIndex[listView.index()].paint;
    return resolvedCollectionSelectionColor(
        item, listView, paint.selectedItemBackgroundColor, paint.hoveredSelectedItemBackgroundColor,
        paint.focusedSelectedItemBackgroundColor, paint.pressedSelectedItemBackgroundColor);
}

[[nodiscard]] UIPremultipliedRgba8Color
UIContext::Impl::resolvedVirtualGridViewSelectionColor(UINodeId item) const noexcept
{
    const UINodeId virtualGridView = virtualGridViewForItem(item);
    const VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr || !isSelectedVirtualGridViewItem(item))
    {
        return {};
    }
    return resolvedCollectionSelectionColor(
        item, virtualGridView,
        state->paint.selectedItemBackgroundColor,
        state->paint.hoveredSelectedItemBackgroundColor,
        state->paint.focusedSelectedItemBackgroundColor,
        state->paint.pressedSelectedItemBackgroundColor);
}

[[nodiscard]] UIPremultipliedRgba8Color
UIContext::Impl::resolvedDataGridRowSelectionColor(UINodeId cell) const noexcept
{
    const UINodeId dataGrid = dataGridForCell(cell);
    const DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state == nullptr || !isSelectedDataGridRowCell(cell))
    {
        return {};
    }
    UIStraightSrgba8Color color = state->paint.selectedRowBackgroundColor;
    if (isFocusVisible(dataGrid) &&
        state->paint.focusedSelectedRowBackgroundColor.alpha != 0)
    {
        color = state->paint.focusedSelectedRowBackgroundColor;
    }
    if (isHoveredDataGridRowCell(cell) &&
        state->paint.hoveredSelectedRowBackgroundColor.alpha != 0)
    {
        color = state->paint.hoveredSelectedRowBackgroundColor;
    }
    return widgetPaintColor(cell, premultiply(color));
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedTreeViewSelectionColor(UINodeId item) const noexcept
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || treeView.index() >= treeViewStatesByNodeIndex.size() ||
        !isSelectedTreeViewItem(item))
    {
        return {};
    }
    const UITreeViewPaint& paint = treeViewStatesByNodeIndex[treeView.index()].paint;
    return resolvedCollectionSelectionColor(
        item, treeView, paint.selectedItemBackgroundColor, paint.hoveredSelectedItemBackgroundColor,
        paint.focusedSelectedItemBackgroundColor, paint.pressedSelectedItemBackgroundColor);
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::resolvedTreeViewDisclosureColor(UINodeId item) const noexcept
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size())
    {
        return {};
    }
    const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
    if (!itemState.bound || !itemState.expandable)
    {
        return {};
    }
    return widgetPaintColor(item, premultiply(treeViewStatesByNodeIndex[treeView.index()].paint.disclosureColor));
}

[[nodiscard]] ProductChromeStorage UIContext::Impl::productChromeStorageFor(u32 index) noexcept
{
    TabState* tab = tabViewStorage.tryTab(idForIndex(index));
    SplitterState* splitter = splitViewStorage.trySplitter(idForIndex(index));
    std::optional<std::reference_wrapper<UITabPaint>> tabPaint;
    std::optional<std::reference_wrapper<UISplitterPaint>> splitterPaint;
    if (tab != nullptr)
    {
        tabPaint = std::ref(tab->paint);
    }
    if (splitter != nullptr)
    {
        splitterPaint = std::ref(splitter->paint);
    }
    return {
        .box = boxPaintsByIndex[index],
        .text = textStatesByIndex[index].style,
        .button = buttonPaintsByNodeIndex[index],
        .checkbox = checkboxPaintsByNodeIndex[index],
        .slider = sliderPaintsByNodeIndex[index],
        .progressBar = progressBarStatesByNodeIndex[index].paint,
        .radioButton = radioButtonStatesByNodeIndex[index].paint,
        .scrollView = scrollViewPaintsByNodeIndex[index],
        .dropdown = dropdownStatesByNodeIndex[index].paint,
        .listView = listViewStatesByNodeIndex[index].paint,
        .treeView = treeViewStatesByNodeIndex[index].paint,
        .textEdit = textEditPaintsByNodeIndex[index],
        .tab = tabPaint,
        .splitter = splitterPaint,
        .imageTint = [&]() noexcept -> UIStraightSrgba8Color* {
            UIImageContent* image = imageContentStorage.getMutable(index);
            return image == nullptr ? nullptr : &image->tint;
        }(),
        .virtualGridView = [&]() noexcept -> UIVirtualGridViewPaint* {
            VirtualGridViewState* state =
                virtualGridViewStorage.tryView(idForIndex(index));
            return state != nullptr ? &state->paint : nullptr;
        }(),
        .dataGrid = [&]() noexcept -> UIDataGridPaint* {
            DataGridState* state =
                dataGridStorage.tryGrid(idForIndex(index));
            return state != nullptr ? &state->paint : nullptr;
        }(),
    };
}

void UIContext::Impl::applyProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                  u16 affectedBindings, u16 targetBindings) noexcept
{
    if (index >= themeBindingsByNodeIndex.size())
    {
        return;
    }
    ProductChromeStorage storage = productChromeStorageFor(index);
    const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
        storage, role, theme, affectedBindings, targetBindings);
    Detail::applyProductChromeTransition(storage, transition, affectedBindings);
}

void UIContext::Impl::applyDefaultProductChrome(u32 index, UIStyleRoleId role) noexcept
{
    if (index >= themeBindingsByNodeIndex.size())
    {
        return;
    }
    const u16 bindings = defaultThemeBindingsFor(role);
    themeBindingsByNodeIndex[index] = bindings;
    applyProductChromeTransition(index, role, productTheme, bindings, bindings);
}

void UIContext::Impl::stageThemePaintChange(u32 index) noexcept
{
    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyPaint;
}

void UIContext::Impl::detachThemeBinding(u32 index, u16 binding) noexcept
{
    if (index < themeBindingsByNodeIndex.size())
    {
        themeBindingsByNodeIndex[index] &= static_cast<u16>(~binding);
        styleOverridesByNodeIndex[index] |= binding;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::stageThemeTextStyle(u32 index, const UITextStyle& nextStyle)
{
    WidgetTextState& state = textStatesByIndex[index];
    if (state.style == nextStyle)
    {
        return Core::success();
    }
    stageThemePaintChange(index);
    if (!state.hasContent || !Detail::textMeasureInputsDiffer(state.style, nextStyle))
    {
        themeTextMetricsScratchByNodeIndex[index] = state.metrics;
        return Core::success();
    }
    auto measured = measureWidgetText(textViewFor(index), nextStyle);
    if (!measured)
    {
        return Core::failure(measured.error());
    }
    themeTextMetricsScratchByNodeIndex[index] = *measured;
    if (*measured != state.metrics)
    {
        themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::stageProductChromeTransition(u32 index, UIStyleRoleId role,
                                                        const UITheme& theme, u16 affectedBindings,
                                                        u16 targetBindings)
{
    if (index >= themeBindingsByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI Theme node index is out of range");
    }

    ProductChromeStorage storage = productChromeStorageFor(index);
    const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
        storage, role, theme, affectedBindings, targetBindings);
    if ((affectedBindings & ThemeBindingTextStyle) != 0)
    {
        if (Core::Status status = stageThemeTextStyle(index, transition.target.text); !status)
        {
            return status;
        }
    }

    constexpr u16 NonTextBindings = static_cast<u16>(~ThemeBindingTextStyle);
    if ((transition.changedBindings & NonTextBindings) != 0)
    {
        stageThemePaintChange(index);
    }
    if ((transition.layoutAffectingBindings & NonTextBindings) != 0)
    {
        themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
    }
    return Core::success();
}

void UIContext::Impl::applyStagedProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                        u16 affectedBindings, u16 targetBindings) noexcept
{
    ProductChromeStorage storage = productChromeStorageFor(index);
    const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
        storage, role, theme, affectedBindings, targetBindings);
    if ((affectedBindings & ThemeBindingTextStyle) != 0)
    {
        WidgetTextState& textState = textStatesByIndex[index];
        if (textState.hasContent && Detail::textMeasureInputsDiffer(textState.style, transition.target.text))
        {
            textState.metrics = themeTextMetricsScratchByNodeIndex[index];
        }
    }
    Detail::applyProductChromeTransition(storage, transition, affectedBindings);
}

void UIContext::Impl::propagateThemeLayoutDirtyToAncestors() noexcept
{
    for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
    {
        if ((themeDirtyScratchByNodeIndex[index] & ThemeDirtyLayoutSelf) == 0)
        {
            continue;
        }
        const NodeRecord* record = recordByIndex(index);
        u32 parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
        while (parentIndex != InvalidNodeIndex)
        {
            themeDirtyScratchByNodeIndex[parentIndex] |= ThemeDirtyLayoutAncestor;
            record = recordByIndex(parentIndex);
            parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
        }
    }
}

[[nodiscard]] Core::Status UIContext::Impl::preflightThemeDirtyQueue()
{
    compactDirtyQueue();
    usize requiredQueueEntries = 0;
    for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
    {
        if (themeDirtyScratchByNodeIndex[index] != 0 && !dirtyQueueStorage.isQueued(index) &&
            !dirtyQueueStorage.isReserved(index))
        {
            ++requiredQueueEntries;
        }
    }
    const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI Theme update exceeds dirty queue capacity");
    }
    return Core::success();
}

void UIContext::Impl::publishThemeDirtyState() noexcept
{
    constexpr UIDirty ChangedNodeLayoutDirty = UIDirty::Style | UIDirty::Measure | UIDirty::Arrange |
                                               UIDirty::Composite | UIDirty::HitTest | UIDirty::Semantics;
    constexpr UIDirty AncestorLayoutDirty =
        UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite | UIDirty::HitTest;
    bool hasLayoutChange = false;
    bool hasPaintChange = false;
    for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
    {
        const u8 staged = themeDirtyScratchByNodeIndex[index];
        if (staged == 0)
        {
            continue;
        }
        if (!dirtyQueueStorage.isQueued(index))
        {
            dirtyQueueStorage.enqueue(idForIndex(index));
        }
        if ((staged & ThemeDirtyLayoutSelf) != 0)
        {
            dirtyQueueStorage.flags(index) |= ChangedNodeLayoutDirty;
            hasLayoutChange = true;
        } else if ((staged & ThemeDirtyLayoutAncestor) != 0)
        {
            dirtyQueueStorage.flags(index) |= AncestorLayoutDirty;
            hasLayoutChange = true;
        }
        if ((staged & ThemeDirtyPaint) != 0)
        {
            dirtyQueueStorage.flags(index) |= UIDirty::Paint | UIDirty::Semantics;
            hasPaintChange = true;
        }
    }
    if (hasLayoutChange)
    {
        phaseDirty |= PhaseLayout | PhaseHit;
    }
    if (hasPaintChange)
    {
        phaseDirty |= PhasePaint | PhaseSemantics;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::setProductTheme(const UITheme& theme)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status validation = Detail::validateProductTheme(theme); !validation)
    {
        return validation;
    }
    if (theme.density != productTheme.density && liveRootCount != 0)
    {
        return fail(UIErrorCode::InvalidTheme,
                    "UI Theme density cannot change while a live root exists; rebuild the root");
    }
    if (productTheme == theme)
    {
        return Core::success();
    }

    std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
    for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
    {
        const NodeRecord* record = recordByIndex(index);
        const u16 bindings = themeBindingsByNodeIndex[index];
        if (record == nullptr || bindings == 0)
        {
            continue;
        }
        if (Core::Status staged =
                stageProductChromeTransition(index, styleRolesByNodeIndex[index], theme, bindings, bindings);
            !staged)
        {
            return staged;
        }
    }
    propagateThemeLayoutDirtyToAncestors();
    if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
    {
        return capacity;
    }

    for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
    {
        const NodeRecord* record = recordByIndex(index);
        const u16 bindings = themeBindingsByNodeIndex[index];
        if (record == nullptr || bindings == 0)
        {
            continue;
        }
        const UIStyleRoleId role = styleRolesByNodeIndex[index];
        applyStagedProductChromeTransition(index, role, theme, bindings, bindings);
    }
    productTheme = theme;
    publishThemeDirtyState();
    return Core::success();
}

[[nodiscard]] Core::Result<UIStyleClassId> UIContext::Impl::registerStyleClass()
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (styleRegistrationClosed)
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI style classes must be registered before creating retained nodes");
    }
    return styleSheetStorage.registerClass();
}

[[nodiscard]] Core::Result<UIStyleTokenId>
UIContext::Impl::registerStyleColorToken(UIStraightSrgba8Color value)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (styleRegistrationClosed)
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI style tokens must be registered before creating retained nodes");
    }
    return styleSheetStorage.registerColorToken(value);
}

[[nodiscard]] Core::Result<UIStraightSrgba8Color>
UIContext::Impl::styleColorToken(UIStyleTokenId token) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    return styleSheetStorage.colorToken(token);
}

[[nodiscard]] u32 UIContext::Impl::tokenDependencyHead(UIStyleTokenId token,
                                      const std::pmr::vector<u32>& headByToken) const noexcept
{
    if (!token.hasValue())
    {
        return 0;
    }
    const usize tokenSlot = token.value - 1U;
    if (tokenSlot >= headByToken.size())
    {
        return 0;
    }
    return headByToken[tokenSlot];
}

[[nodiscard]] Core::Status UIContext::Impl::preflightStyleColorTokenDirtyQueue(
    UIStyleTokenId token, StyleTokenUpdateStatistics& statistics)
{
    compactDirtyQueue();
    usize requiredQueueEntries = 0;
    const auto visitList = [&](u32 head, const std::pmr::vector<u32>& nextByNode,
                               auto isSuppressed) {
        for (u32 link = head; link != 0;)
        {
            const u32 index = link - 1U;
            if (index >= nextByNode.size())
            {
                break;
            }
            link = nextByNode[index];
            ++statistics.inspectedNodeCount;
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || isSuppressed(index, *record))
            {
                continue;
            }
            // Reverse index already stores the winning token; no full-tree
            // resolve or candidate-rule scan is required on the update path.
            ++statistics.resolvedNodeCount;
            ++statistics.affectedNodeCount;
            if (!dirtyQueueStorage.isQueued(index) &&
                !dirtyQueueStorage.isReserved(index))
            {
                ++requiredQueueEntries;
            }
        }
    };
    visitList(tokenDependencyHead(token, styleTokenDependencyHeadByTokenIndex),
              styleTokenDependencyNextByNodeIndex,
              [this](u32 index, const NodeRecord& record) {
                  return hasLocalBoxFillOverride(index, record);
              });
    visitList(tokenDependencyHead(token, imageTintTokenDependencyHeadByTokenIndex),
              imageTintTokenDependencyNextByNodeIndex,
              [this](u32 index, const NodeRecord&) {
                  return hasLocalImageTintOverride(index);
              });

    const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries >
            dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI style token update exceeds dirty queue capacity");
    }
    return Core::success();
}

void UIContext::Impl::publishStyleColorTokenDirtyState(UIStyleTokenId token) noexcept
{
    bool changed = false;
    const auto publishList = [&](u32 head, const std::pmr::vector<u32>& nextByNode,
                                 auto isSuppressed) {
        for (u32 link = head; link != 0;)
        {
            const u32 index = link - 1U;
            if (index >= nextByNode.size())
            {
                break;
            }
            link = nextByNode[index];
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || isSuppressed(index, *record))
            {
                continue;
            }
            if (!dirtyQueueStorage.isQueued(index))
            {
                dirtyQueueStorage.enqueue(idForIndex(index));
            }
            dirtyQueueStorage.flags(index) |= UIDirty::Paint;
            changed = true;
        }
    };
    publishList(tokenDependencyHead(token, styleTokenDependencyHeadByTokenIndex),
                styleTokenDependencyNextByNodeIndex,
                [this](u32 index, const NodeRecord& record) {
                    return hasLocalBoxFillOverride(index, record);
                });
    publishList(tokenDependencyHead(token, imageTintTokenDependencyHeadByTokenIndex),
                imageTintTokenDependencyNextByNodeIndex,
                [this](u32 index, const NodeRecord&) {
                    return hasLocalImageTintOverride(index);
                });
    if (changed)
    {
        phaseDirty |= PhasePaint;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::setStyleColorToken(
    UIStyleTokenId token, UIStraightSrgba8Color value)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();

    auto current = styleSheetStorage.colorToken(token);
    if (!current)
    {
        return Core::failure(current.error());
    }
    lastStyleTokenUpdateInspectedNodeCount = 0;
    lastStyleTokenUpdateResolvedNodeCount = 0;
    lastStyleTokenUpdateAffectedNodeCount = 0;
    lastStyleTokenUpdateCandidateRuleCount = 0;
    if (*current == value)
    {
        return Core::success();
    }

    StyleTokenUpdateStatistics statistics{};
    const Core::Status capacity =
        preflightStyleColorTokenDirtyQueue(token, statistics);
    lastStyleTokenUpdateInspectedNodeCount = statistics.inspectedNodeCount;
    lastStyleTokenUpdateResolvedNodeCount = statistics.resolvedNodeCount;
    lastStyleTokenUpdateAffectedNodeCount = statistics.affectedNodeCount;
    lastStyleTokenUpdateCandidateRuleCount = statistics.candidateRuleCount;
    if (!capacity)
    {
        return capacity;
    }
    if (Core::Status update = styleSheetStorage.setColorToken(token, value);
        !update)
    {
        return update;
    }
    publishStyleColorTokenDirtyState(token);
    return Core::success();
}

[[nodiscard]] Core::MonotonicTimePoint UIContext::Impl::motionNow() const noexcept
{
    const Core::IMonotonicClock* clock =
        motionClock != nullptr ? motionClock : &motionDefaultClock;
    return clock->now();
}

[[nodiscard]] Core::Status UIContext::Impl::installStyleSheet(
    std::span<const UIStyleBoxFillRule> rules)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (styleRegistrationClosed)
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI stylesheet must be installed before creating retained nodes");
    }
    return styleSheetStorage.compile(rules);
}

} // namespace Tina::UI
