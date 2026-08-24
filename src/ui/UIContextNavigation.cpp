#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveTabView(UINodeId tabView)
{
    auto nodeResult = resolveNode(tabView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::TabView ||
        !tabViewStorage.containsTabView(tabView))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView API requires a TabView node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveTab(UINodeId tab)
{
    auto nodeResult = resolveNode(tab);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Tab ||
        !tabViewStorage.containsTab(tab))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView API requires a Tab node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::validateTabViewUpdaterRoot(
    UINodeId updaterRoot, UINodeId tabView) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto tabViewResult = const_cast<Impl*>(this)->resolveTabView(tabView);
    if (!tabViewResult)
    {
        return Core::failure(tabViewResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, tabView))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI TabView is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTabPaintFromUpdater(
    UINodeId updaterRoot, UINodeId tab, const UITabPaint& paint)
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
    auto tabResult = resolveTab(tab);
    if (!tabResult)
    {
        return Core::failure(tabResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, tab))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Tab is not owned by the updater root");
    }
    UITabPaint& current = tabViewStorage.tabPaintByIndex(tab.index());
    if (current == paint)
    {
        detachThemeBinding(tab.index(), ThemeBindingTabPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(tab); !dirty)
    {
        return dirty;
    }
    current = paint;
    detachThemeBinding(tab.index(), ThemeBindingTabPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UITabPaint> UIContext::Impl::tabPaintFromUpdater(
    UINodeId updaterRoot, UINodeId tab) const
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
    auto tabResult = const_cast<Impl*>(this)->resolveTab(tab);
    if (!tabResult)
    {
        return Core::failure(tabResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, tab))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Tab is not owned by the updater root");
    }
    return tabViewStorage.tabPaintByIndex(tab.index());
}

[[nodiscard]] Core::Status UIContext::Impl::setTabViewItemsFromUpdater(
    UINodeId updaterRoot, UINodeId tabView,
    std::span<const UITabViewItem> items, u32 activeIndex)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return valid;
    }
    if (items.empty())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView requires at least one tab item; use clearTabViewItems to clear it");
    }
    if (items.size() > static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI TabView item capacity has been exceeded");
    }
    if (activeIndex >= items.size())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView active index is out of range");
    }

    const NodeRecord* viewRecord = nodes.tryGet(tabView.storageId());
    if (viewRecord == nullptr)
    {
        return fail(UIErrorCode::InvalidNode, "UI TabView is stale");
    }
    for (usize i = 0; i < items.size(); ++i)
    {
        const UITabViewItem item = items[i];
        if (!item.hasValue() || item.tab == item.panel || item.tab == tabView || item.panel == tabView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI TabView items must contain distinct live tab and panel nodes");
        }
        auto tabResult = resolveTab(item.tab);
        auto panelResult = resolveNode(item.panel);
        if (!tabResult)
        {
            return Core::failure(tabResult.error());
        }
        if (!panelResult)
        {
            return Core::failure(panelResult.error());
        }
        const NodeRecord* tabRecord = *tabResult;
        const NodeRecord* panelRecord = *panelResult;
        if (tabRecord->rootIndex != viewRecord->rootIndex || panelRecord->rootIndex != viewRecord->rootIndex)
        {
            return fail(UIErrorCode::InvalidNode,
                        "UI TabView items must be owned by the updater root");
        }
        if (tabRecord->parentIndex != tabView.index() || panelRecord->parentIndex != tabView.index())
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI TabView tabs and panels must be direct children of the TabView");
        }
        if (panelRecord->kind == BuiltinElementKind::Tab ||
            panelRecord->kind == BuiltinElementKind::TabView ||
            panelRecord->kind == BuiltinElementKind::ListViewItem ||
            panelRecord->kind == BuiltinElementKind::TreeViewItem)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI TabView panels must be ordinary content nodes");
        }
        for (usize previous = 0; previous < i; ++previous)
        {
            if (items[previous].tab == item.tab || items[previous].panel == item.panel ||
                items[previous].tab == item.panel || items[previous].panel == item.tab)
            {
                return fail(UIErrorCode::InvalidParent,
                            "UI TabView items must not duplicate tab or panel nodes");
            }
        }
        const UINodeId existingTabOwner = tabViewStorage.tabViewForTab(item.tab);
        if (existingTabOwner.hasValue() && existingTabOwner != tabView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Tab already belongs to another TabView");
        }
        const UINodeId existingPanelOwner = tabViewStorage.tabViewForPanel(item.panel);
        if (existingPanelOwner.hasValue() && existingPanelOwner != tabView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI panel already belongs to another TabView");
        }
    }

    usize directChildCount = 0;
    for (u32 childIndex = viewRecord->firstChildIndex;
         childIndex != InvalidNodeIndex;)
    {
        const NodeRecord* child = recordByIndex(childIndex);
        if (child == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI TabView direct-child chain is invalid");
        }
        ++directChildCount;
        childIndex = child->nextSiblingIndex;
    }
    if (items.size() > (std::numeric_limits<usize>::max)() / 2U ||
        directChildCount != items.size() * 2U)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI TabView direct children must be exactly the declared tab/panel pairs");
    }

    const TabViewState* current = tabViewStorage.tryTabView(tabView);
    if (current != nullptr && current->itemCount == items.size() &&
        current->activeTab == items[activeIndex].tab)
    {
        bool same = true;
        for (u32 i = 0; i < current->itemCount; ++i)
        {
            if (tabViewStorage.itemAt(tabView, i) != items[i])
            {
                same = false;
                break;
            }
        }
        if (same)
        {
            return Core::success();
        }
    }
    if (Core::Status dirty = markLayoutDirtyBatch({tabView}); !dirty)
    {
        return dirty;
    }
    tabViewStorage.linkValidated(tabView, items, activeIndex);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearTabViewItemsFromUpdater(
    UINodeId updaterRoot, UINodeId tabView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return valid;
    }
    if (tabViewStorage.itemCount(tabView) == 0)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutDirtyBatch({tabView}); !dirty)
    {
        return dirty;
    }
    tabViewStorage.unlinkTabView(tabView);
    return Core::success();
}

[[nodiscard]] Core::Result<u32> UIContext::Impl::tabViewItemCountFromUpdater(
    UINodeId updaterRoot, UINodeId tabView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    return tabViewStorage.itemCount(tabView);
}

[[nodiscard]] Core::Result<UITabViewItem> UIContext::Impl::tabViewItemAtFromUpdater(
    UINodeId updaterRoot, UINodeId tabView, u32 index) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    const u32 count = tabViewStorage.itemCount(tabView);
    if (index >= count)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView item index is out of range");
    }
    return tabViewStorage.itemAt(tabView, index);
}

[[nodiscard]] Core::Status UIContext::Impl::setTabViewActiveTabFromUpdater(
    UINodeId updaterRoot, UINodeId tabView, UINodeId tab)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return valid;
    }
    if (!tab.hasValue() || tabViewStorage.tabViewForTab(tab) != tabView)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Tab does not belong to the TabView");
    }
    const UINodeId previousTab = tabViewStorage.activeTab(tabView);
    if (previousTab == tab)
    {
        return Core::success();
    }
    const UINodeId previousPanel = tabViewStorage.activePanel(tabView);
    const TabState* nextState = tabViewStorage.tryTab(tab);
    const UINodeId nextPanel = nextState != nullptr ? nextState->panel : UINodeId{};
    if (Core::Status dirty = markLayoutDirtyBatch({tabView, previousTab, tab, previousPanel, nextPanel}); !dirty)
    {
        return dirty;
    }
    if (!tabViewStorage.setActiveTab(tabView, tab))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView active tab could not be changed");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::tabViewActiveTabFromUpdater(
    UINodeId updaterRoot, UINodeId tabView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    return tabViewStorage.activeTab(tabView);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::tabViewActivePanelFromUpdater(
    UINodeId updaterRoot, UINodeId tabView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    return tabViewStorage.activePanel(tabView);
}

[[nodiscard]] Core::Result<UITabViewMetrics> UIContext::Impl::tabViewMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId tabView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    return tabViewStorage.committedMetrics(tabView);
}

[[nodiscard]] Core::Result<UITabViewCommandResult> UIContext::Impl::routeTabViewCommandFromUpdater(
    UINodeId updaterRoot, UINodeId tabView, UITabViewCommand command)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI TabView command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTabViewUpdaterRoot(updaterRoot, tabView); !valid)
    {
        return Core::failure(valid.error());
    }
    if (!isValidTabViewCommand(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView command is not recognized");
    }
    const TabViewState* view = tabViewStorage.tryTabView(tabView);
    const u32 count = tabViewStorage.itemCount(tabView);
    if (view == nullptr || count == 0)
    {
        return UITabViewCommandResult{.tabView = tabView};
    }
    const UINodeId focusedTab =
        tabViewStorage.tabViewForTab(defaultActionFocusButton) == tabView
            ? defaultActionFocusButton
            : UINodeId{};
    const UINodeId currentTab = focusedTab.hasValue()
                                    ? focusedTab
                                    : tabViewStorage.activeTab(tabView);
    const TabState* currentState = tabViewStorage.tryTab(currentTab);
    const u32 currentIndex = currentState != nullptr ? currentState->ordinal : 0;
    constexpr u32 InvalidTabIndex = (std::numeric_limits<u32>::max)();
    u32 targetIndex = InvalidTabIndex;
    const auto isFocusableTabAt = [&](u32 index) noexcept {
        const UITabViewItem candidate = tabViewStorage.itemAt(tabView, index);
        return candidate.hasValue() && isCommittedKeyboardFocusCandidate(candidate.tab);
    };
    switch (command)
    {
    case UITabViewCommand::Previous:
        for (u32 step = 1; step <= count; ++step)
        {
            if (!view->config.wrapKeyboardNavigation && step > currentIndex)
            {
                break;
            }
            const u32 candidateIndex = view->config.wrapKeyboardNavigation
                                           ? static_cast<u32>((static_cast<u64>(currentIndex) + count -
                                                               (step % count)) % count)
                                           : currentIndex - step;
            if (isFocusableTabAt(candidateIndex))
            {
                targetIndex = candidateIndex;
                break;
            }
        }
        break;
    case UITabViewCommand::Next:
        for (u32 step = 1; step <= count; ++step)
        {
            if (!view->config.wrapKeyboardNavigation &&
                static_cast<u64>(currentIndex) + step >= count)
            {
                break;
            }
            const u32 candidateIndex = view->config.wrapKeyboardNavigation
                                           ? static_cast<u32>((static_cast<u64>(currentIndex) + step) % count)
                                           : currentIndex + step;
            if (isFocusableTabAt(candidateIndex))
            {
                targetIndex = candidateIndex;
                break;
            }
        }
        break;
    case UITabViewCommand::First:
        for (u32 index = 0; index < count; ++index)
        {
            if (isFocusableTabAt(index))
            {
                targetIndex = index;
                break;
            }
        }
        break;
    case UITabViewCommand::Last:
        for (u32 index = count; index > 0; --index)
        {
            if (isFocusableTabAt(index - 1))
            {
                targetIndex = index - 1;
                break;
            }
        }
        break;
    }
    if (targetIndex == InvalidTabIndex)
    {
        return UITabViewCommandResult{
            .targeted = true,
            .consumed = true,
            .tabView = tabView,
            .tab = currentTab,
        };
    }
    const UITabViewItem item = tabViewStorage.itemAt(tabView, targetIndex);
    if (!item.hasValue())
    {
        return fail(Core::CoreErrorCode::Internal, "UI TabView relationship is invalid");
    }
    const bool focusChanged = defaultActionFocusButton != item.tab;
    const bool selectionChanged =
        view->config.activationMode == UITabActivationMode::Automatic &&
        tabViewStorage.activeTab(tabView) != item.tab;
    if (focusChanged || selectionChanged)
    {
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit(
            [this]() noexcept { releaseRouteDirtyQueueReservations(); });
        if (selectionChanged)
        {
            addTabActivationDirtyReservationCandidates(item.tab);
        }
        if (focusChanged)
        {
            addRouteDirtyReservationCandidate(defaultActionFocusButton);
            addRouteDirtyReservationCandidate(textInputFocus);
            addRouteDirtyReservationCandidate(item.tab);
        }
        if (Core::Status reserved = reserveRouteDirtyQueueSlots(); !reserved)
        {
            return Core::failure(reserved.error());
        }
        if (selectionChanged)
        {
            const UINodeId previousTab = tabViewStorage.activeTab(tabView);
            const UINodeId previousPanel = tabViewStorage.activePanel(tabView);
            const TabState* next = tabViewStorage.tryTab(item.tab);
            const UINodeId nextPanel = next != nullptr ? next->panel : UINodeId{};
            if (Core::Status dirty = markLayoutDirtyBatch(
                    {tabView, previousTab, item.tab, previousPanel, nextPanel});
                !dirty)
            {
                return Core::failure(dirty.error());
            }
        }
        if (focusChanged)
        {
            if (Core::Status focused = applyExplicitFocus(item.tab); !focused)
            {
                return Core::failure(focused.error());
            }
        }
        if (selectionChanged && !tabViewStorage.setActiveTab(tabView, item.tab))
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI TabView relationship rejected a validated command");
        }
    }
    return UITabViewCommandResult{
        .targeted = true,
        .consumed = true,
        .focusChanged = focusChanged,
        .selectionChanged = selectionChanged,
        .tabView = tabView,
        .tab = item.tab,
    };
}

[[nodiscard]] Core::Result<UITabViewCommandResult> UIContext::Impl::routeFocusedTabViewCommand(
    UITabViewCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI TabView command cannot run during pointer routing");
    }
    if (!isValidTabViewCommand(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView command is not recognized");
    }
    if (!pressed)
    {
        return UITabViewCommandResult{
            .consumed = tabViewCommandPressLatch.release(command),
        };
    }
    if (tabViewCommandPressLatch.isLatched(command))
    {
        return UITabViewCommandResult{.consumed = true};
    }

    const UINodeId tabView = tabViewStorage.tabViewForTab(defaultActionFocusButton);
    if (!tabView.hasValue())
    {
        return UITabViewCommandResult{};
    }
    auto routed = routeTabViewCommandFromUpdater(rootForTabView(tabView), tabView, command);
    if (!routed)
    {
        return Core::failure(routed.error());
    }
    if (routed->consumed)
    {
        tabViewCommandPressLatch.latch(command);
    }
    return routed;
}

[[nodiscard]] Core::Result<UITabViewCommandResult> UIContext::Impl::routeFocusedTabViewDirection(
    UIFocusNavigationDirection direction, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI TabView direction cannot run during pointer routing");
    }
    if (!Detail::isValidFocusNavigationDirection(direction))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TabView direction is not recognized");
    }
    if (!pressed)
    {
        return UITabViewCommandResult{
            .consumed = tabViewDirectionPressLatch.release(direction),
        };
    }
    if (tabViewDirectionPressLatch.isLatched(direction))
    {
        return UITabViewCommandResult{.consumed = true};
    }

    const UINodeId tabView = tabViewStorage.tabViewForTab(defaultActionFocusButton);
    if (!tabView.hasValue())
    {
        return UITabViewCommandResult{};
    }
    const TabViewState* state = tabViewStorage.tryTabView(tabView);
    if (state == nullptr)
    {
        return UITabViewCommandResult{};
    }
    const bool horizontal = state->config.placement == UITabViewPlacement::Top ||
                            state->config.placement == UITabViewPlacement::Bottom;
    std::optional<UITabViewCommand> command;
    switch (direction)
    {
    case UIFocusNavigationDirection::Left:
        if (horizontal)
        {
            command = UITabViewCommand::Previous;
        }
        break;
    case UIFocusNavigationDirection::Right:
        if (horizontal)
        {
            command = UITabViewCommand::Next;
        }
        break;
    case UIFocusNavigationDirection::Up:
        if (!horizontal)
        {
            command = UITabViewCommand::Previous;
        }
        break;
    case UIFocusNavigationDirection::Down:
        if (!horizontal)
        {
            command = UITabViewCommand::Next;
        }
        break;
    }
    if (!command.has_value())
    {
        return UITabViewCommandResult{};
    }
    auto routed = routeTabViewCommandFromUpdater(rootForTabView(tabView), tabView, *command);
    if (!routed)
    {
        return Core::failure(routed.error());
    }
    if (routed->consumed)
    {
        tabViewDirectionPressLatch.latch(direction);
    }
    return routed;
}

[[nodiscard]] UINodeId UIContext::Impl::rootForTabView(UINodeId tabView) const noexcept
{
    const NodeRecord* record = nodes.tryGet(tabView.storageId());
    return record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setTabViewItems(
    UINodeId tabView, std::span<const UITabViewItem> items, u32 activeIndex)
{
    return setTabViewItemsFromUpdater(rootForTabView(tabView), tabView, items, activeIndex);
}

[[nodiscard]] Core::Status UIContext::Impl::clearTabViewItems(UINodeId tabView)
{
    return clearTabViewItemsFromUpdater(rootForTabView(tabView), tabView);
}

[[nodiscard]] Core::Result<u32> UIContext::Impl::tabViewItemCount(UINodeId tabView) const
{
    return tabViewItemCountFromUpdater(rootForTabView(tabView), tabView);
}

[[nodiscard]] Core::Result<UITabViewItem> UIContext::Impl::tabViewItemAt(UINodeId tabView, u32 index) const
{
    return tabViewItemAtFromUpdater(rootForTabView(tabView), tabView, index);
}

[[nodiscard]] Core::Status UIContext::Impl::setTabViewActiveTab(UINodeId tabView, UINodeId tab)
{
    return setTabViewActiveTabFromUpdater(rootForTabView(tabView), tabView, tab);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::tabViewActiveTab(UINodeId tabView) const
{
    return tabViewActiveTabFromUpdater(rootForTabView(tabView), tabView);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::tabViewActivePanel(UINodeId tabView) const
{
    return tabViewActivePanelFromUpdater(rootForTabView(tabView), tabView);
}

[[nodiscard]] Core::Result<UITabViewMetrics> UIContext::Impl::tabViewMetrics(UINodeId tabView) const
{
    return tabViewMetricsFromUpdater(rootForTabView(tabView), tabView);
}

[[nodiscard]] Core::Result<UITabViewCommandResult> UIContext::Impl::routeTabViewCommand(
    UINodeId tabView, UITabViewCommand command)
{
    return routeTabViewCommandFromUpdater(rootForTabView(tabView), tabView, command);
}

[[nodiscard]] Core::Result<UIMenuCommandResult> UIContext::Impl::routeMenuCommandFromUpdater(
    UINodeId updaterRoot, UINodeId menu, UIMenuCommand command)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Menu command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return Core::failure(valid.error());
    }
    if (!isValidMenuCommand(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Menu command is not recognized");
    }
    const MenuState* state = menuStorage.tryMenu(menu);
    if (state == nullptr || !menuStorage.isOpen(menu) ||
        menuStorage.activeMenu() != menu)
    {
        return UIMenuCommandResult{.menu = menu};
    }
    if (command == UIMenuCommand::Dismiss ||
        command == UIMenuCommand::CloseSubmenu)
    {
        const UINodeId parentItem = menuStorage.parentItemForMenu(menu);
        const bool shouldClose = command == UIMenuCommand::Dismiss ||
                                 parentItem.hasValue();
        if (shouldClose)
        {
            if (Core::Status closed = setMenuOpenState(menu, false); !closed)
            {
                return Core::failure(closed.error());
            }
        }
        return UIMenuCommandResult{
            .targeted = true,
            .consumed = true,
            .dismissed = shouldClose,
            .menu = menu,
            .focus = defaultActionFocusButton,
        };
    }

    const NodeRecord* menuRecord = nodes.tryGet(menu.storageId());
    if (menuRecord == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI Menu record is unavailable");
    }
    const auto focusableItem = [&](u32 index) noexcept {
        if (index == InvalidNodeIndex)
        {
            return false;
        }
        const UINodeId item = idForIndex(index);
        const MenuItemState* itemState = menuStorage.tryItem(item);
        return itemState != nullptr &&
               itemState->config.kind != UIMenuItemKind::Separator &&
               isCommittedKeyboardFocusCandidate(item);
    };
    const auto firstFocusable = [&](bool reverse) noexcept {
        u32 index = reverse ? menuRecord->lastChildIndex
                            : menuRecord->firstChildIndex;
        usize visited = 0;
        while (index != InvalidNodeIndex && visited++ < nodes.capacity())
        {
            const NodeRecord* child = recordByIndex(index);
            if (child == nullptr)
            {
                break;
            }
            if (focusableItem(index))
            {
                return idForIndex(index);
            }
            index = reverse ? child->previousSiblingIndex
                            : child->nextSiblingIndex;
        }
        return UINodeId{};
    };
    const auto adjacentFocusable = [&](bool reverse) noexcept {
        const MenuItemState* focused = menuStorage.tryItem(defaultActionFocusButton);
        if (focused == nullptr || focused->menu != menu)
        {
            return firstFocusable(reverse);
        }
        const NodeRecord* focusedRecord = nodes.tryGet(defaultActionFocusButton.storageId());
        u32 index = focusedRecord != nullptr
                        ? (reverse ? focusedRecord->previousSiblingIndex
                                   : focusedRecord->nextSiblingIndex)
                        : InvalidNodeIndex;
        usize visited = 0;
        while (index != InvalidNodeIndex && visited++ < nodes.capacity())
        {
            const NodeRecord* child = recordByIndex(index);
            if (child == nullptr)
            {
                break;
            }
            if (focusableItem(index))
            {
                return idForIndex(index);
            }
            index = reverse ? child->previousSiblingIndex
                            : child->nextSiblingIndex;
        }
        return state->config.wrapKeyboardNavigation
                   ? firstFocusable(reverse)
                   : UINodeId{};
    };

    if (command == UIMenuCommand::OpenSubmenu)
    {
        const MenuItemState* focused =
            menuStorage.tryItem(defaultActionFocusButton);
        const UINodeId submenuNode =
            focused != nullptr && focused->menu == menu &&
                    focused->config.kind == UIMenuItemKind::Submenu
                ? menuStorage.submenuForItem(focused->node)
                : UINodeId{};
        if (!hasValidSubmenuRelationship(defaultActionFocusButton,
                                         submenuNode))
        {
            return UIMenuCommandResult{
                .targeted = true,
                .consumed = true,
                .menu = menu,
                .focus = defaultActionFocusButton,
            };
        }

        UINodeId childFocus{};
        const NodeRecord* submenuRecord =
            nodes.tryGet(submenuNode.storageId());
        u32 childIndex = submenuRecord != nullptr
                             ? submenuRecord->firstChildIndex
                             : InvalidNodeIndex;
        usize visited = 0;
        while (childIndex != InvalidNodeIndex &&
               visited++ < nodes.capacity())
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            const UINodeId candidate = idForIndex(childIndex);
            const MenuItemState* candidateState =
                menuStorage.tryItem(candidate);
            if (candidateState != nullptr &&
                candidateState->config.kind != UIMenuItemKind::Separator &&
                isNodeEnabled(candidate) &&
                isAuthoredTooltipNodeVisible(candidate) &&
                hasBehavior(child->behaviors,
                            UIElementBehavior::Focusable))
            {
                childFocus = candidate;
                break;
            }
            childIndex = child->nextSiblingIndex;
        }

        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit(
            [this]() noexcept { releaseRouteDirtyQueueReservations(); });
        addRouteDirtyReservationCandidate(defaultActionFocusButton);
        addRouteDirtyReservationCandidate(textInputFocus);
        addRouteDirtyReservationCandidate(childFocus);
        addRouteLayoutDirtyReservationCandidates(submenuNode);
        addRouteLayoutDirtyReservationCandidates(
            menuPlacementAnchor(submenuNode));
        addActiveMenuBranchDirtyReservationCandidates(
            menuStorage.activeChildMenu(menu));
        addRouteLayoutDirtyReservationCandidates(activePopup());
        addRouteLayoutDirtyReservationCandidates(
            dropdownForPopup(activePopup()));
        if (Core::Status reserved = reserveRouteDirtyQueueSlots(); !reserved)
        {
            return Core::failure(reserved.error());
        }
        if (Core::Status opened =
                setMenuOpenState(submenuNode, true);
            !opened)
        {
            return Core::failure(opened.error());
        }
        const bool focusChanged = childFocus.hasValue() &&
                                  defaultActionFocusButton != childFocus;
        if (focusChanged)
        {
            if (Core::Status focused = applyExplicitFocus(childFocus);
                !focused)
            {
                return Core::failure(focused.error());
            }
        }
        return UIMenuCommandResult{
            .targeted = true,
            .consumed = true,
            .focusChanged = focusChanged,
            .menu = menu,
            .focus = defaultActionFocusButton,
        };
    }

    UINodeId target{};
    switch (command)
    {
    case UIMenuCommand::Previous:
        target = adjacentFocusable(true);
        break;
    case UIMenuCommand::Next:
        target = adjacentFocusable(false);
        break;
    case UIMenuCommand::First:
        target = firstFocusable(false);
        break;
    case UIMenuCommand::Last:
        target = firstFocusable(true);
        break;
    case UIMenuCommand::OpenSubmenu:
    case UIMenuCommand::CloseSubmenu:
    case UIMenuCommand::Dismiss:
        break;
    }
    if (!target.hasValue())
    {
        return UIMenuCommandResult{
            .targeted = true,
            .consumed = true,
            .menu = menu,
            .focus = defaultActionFocusButton,
        };
    }
    const bool focusChanged = defaultActionFocusButton != target;
    if (focusChanged)
    {
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit(
            [this]() noexcept { releaseRouteDirtyQueueReservations(); });
        addRouteDirtyReservationCandidate(defaultActionFocusButton);
        addRouteDirtyReservationCandidate(textInputFocus);
        addRouteDirtyReservationCandidate(target);
        if (Core::Status reserved = reserveRouteDirtyQueueSlots(); !reserved)
        {
            return Core::failure(reserved.error());
        }
        if (Core::Status focused = applyExplicitFocus(target); !focused)
        {
            return Core::failure(focused.error());
        }
    }
    return UIMenuCommandResult{
        .targeted = true,
        .consumed = true,
        .focusChanged = focusChanged,
        .menu = menu,
        .focus = target,
    };
}

[[nodiscard]] Core::Result<UIMenuCommandResult> UIContext::Impl::routeMenuCommand(
    UINodeId menu, UIMenuCommand command)
{
    const NodeRecord* record = contains(menu) ? nodes.tryGet(menu.storageId()) : nullptr;
    const UINodeId root = record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    return routeMenuCommandFromUpdater(root, menu, command);
}

[[nodiscard]] Core::Result<UIMenuCommandResult> UIContext::Impl::routeMenuCommand(
    UIMenuCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!isValidMenuCommand(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Menu command is not recognized");
    }
    if (!pressed)
    {
        return UIMenuCommandResult{
            .consumed = menuCommandPressLatch.release(command),
        };
    }
    if (menuCommandPressLatch.isLatched(command))
    {
        return UIMenuCommandResult{.consumed = true};
    }
    const UINodeId menu = menuStorage.activeMenu();
    if (!menu.hasValue())
    {
        return UIMenuCommandResult{};
    }
    auto routed = routeMenuCommand(menu, command);
    if (!routed)
    {
        return Core::failure(routed.error());
    }
    if (routed->consumed)
    {
        menuCommandPressLatch.latch(command);
    }
    return routed;
}

[[nodiscard]] Core::Result<UIMenuInvocationResult> UIContext::Impl::routeMenuInvocation(
    UIMenuInvocationCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!isValidMenuInvocationCommand(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Menu invocation command is not recognized");
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Menu invocation cannot run during pointer routing");
    }
    drainDeferredRootDestroys();
    if (!pressed)
    {
        return UIMenuInvocationResult{
            .consumed = menuInvocationPressLatch.release(command),
        };
    }
    if (menuInvocationPressLatch.isLatched(command))
    {
        return UIMenuInvocationResult{.consumed = true};
    }
    if (Core::Status modality = setInputModality(UIInputModality::Keyboard);
        !modality)
    {
        return Core::failure(modality.error());
    }

    const auto [menu, anchor] = contextMenuForTarget(defaultActionFocus());
    if (!menu.hasValue())
    {
        return UIMenuInvocationResult{};
    }
    const bool wasOpen = menuStorage.isOpen(menu);
    if (Core::Status opened = setMenuOpenState(menu, true); !opened)
    {
        return Core::failure(opened.error());
    }
    menuInvocationPressLatch.latch(command);
    return UIMenuInvocationResult{
        .targeted = true,
        .consumed = true,
        .opened = !wasOpen,
        .menu = menu,
        .anchor = anchor,
    };
}

[[nodiscard]] Core::Status UIContext::Impl::setTabPaint(UINodeId tab, const UITabPaint& paint)
{
    return setTabPaintFromUpdater(rootForTabView(tab), tab, paint);
}

[[nodiscard]] Core::Result<UITabPaint> UIContext::Impl::tabPaint(UINodeId tab) const
{
    return tabPaintFromUpdater(rootForTabView(tab), tab);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveScrollView(UINodeId scrollView)
{
    auto nodeResult = resolveNode(scrollView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::ScrollView ||
        scrollView.index() >= scrollViewPaintsByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ScrollView API requires a ScrollView node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveScroll(UINodeId node)
{
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!hasBehavior((*nodeResult)->behaviors, UIElementBehavior::Scroll) ||
        behaviorStateStorage.tryScrollState(node.index()) == nullptr)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Scroll API requires a Scroll-capable node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::markScrollOffsetDirty(UINodeId scrollView)
{
    if (Core::Status dirty = markHitTestDirty(scrollView); !dirty)
    {
        return dirty;
    }
    const u32 index = scrollView.index();
    dirtyQueueStorage.flags(index) |=
        UIDirty::Arrange | UIDirty::Composite | UIDirty::Paint | UIDirty::Semantics;
    phaseDirty |= PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::markLayoutStyleDirty(UINodeId node)
{
    return markLayoutDirtyBatch({node});
}

[[nodiscard]] bool UIContext::Impl::isLiveScrollView(UINodeId scrollView) const noexcept
{
    if (!scrollView.hasValue() || !contains(scrollView) || scrollView.index() >= scrollViewPaintsByNodeIndex.size() ||
        behaviorStateStorage.tryScrollState(scrollView.index()) == nullptr)
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(scrollView.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::ScrollView;
}

[[nodiscard]] bool UIContext::Impl::isLiveListView(UINodeId listView) const noexcept
{
    if (!listView.hasValue() || !contains(listView) || listView.index() >= listViewStatesByNodeIndex.size())
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(listView.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::ListView;
}

[[nodiscard]] bool UIContext::Impl::isLiveTreeView(UINodeId treeView) const noexcept
{
    if (!treeView.hasValue() || !contains(treeView) || treeView.index() >= treeViewStatesByNodeIndex.size())
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(treeView.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::TreeView;
}

[[nodiscard]] bool UIContext::Impl::isLiveVirtualGridView(
    UINodeId virtualGridView) const noexcept
{
    if (!virtualGridView.hasValue() || !contains(virtualGridView))
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(virtualGridView.storageId());
    return record != nullptr &&
           record->kind == BuiltinElementKind::VirtualGridView &&
           virtualGridViewStorage.tryView(virtualGridView) != nullptr;
}

[[nodiscard]] bool UIContext::Impl::isLiveDataGrid(UINodeId dataGrid) const noexcept
{
    if (!dataGrid.hasValue() || !contains(dataGrid))
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(dataGrid.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::DataGrid &&
           dataGridStorage.tryGrid(dataGrid) != nullptr;
}

[[nodiscard]] bool UIContext::Impl::isLiveVirtualView(UINodeId node) const noexcept
{
    return isLiveListView(node) || isLiveTreeView(node) ||
           isLiveVirtualGridView(node) || isLiveDataGrid(node);
}

[[nodiscard]] bool UIContext::Impl::isLiveVerticalVirtualView(UINodeId node) const noexcept
{
    return isLiveListView(node) || isLiveTreeView(node) ||
           isLiveVirtualGridView(node);
}

[[nodiscard]] bool UIContext::Impl::isLiveScrollable(UINodeId node) const noexcept
{
    return isLiveScrollView(node) || isLiveVirtualView(node);
}

[[nodiscard]] bool UIContext::Impl::isLiveMultilineTextEdit(UINodeId node) const noexcept
{
    if (!node.hasValue() || !contains(node) || !isLiveTextEdit(node) ||
        node.index() >= textEditMultilineByNodeIndex.size())
    {
        return false;
    }
    const UITextEditMultilineConfig& config = textEditMultilineByNodeIndex[node.index()];
    return config.enabled && config.verticalScrollEnabled &&
           node.index() < textEditVisualLayoutsByNodeIndex.size() &&
           textEditVisualLayoutsByNodeIndex[node.index()].maximumScrollY > 0.0F;
}

[[nodiscard]] bool UIContext::Impl::textEditWheelWouldChange(UINodeId textEdit, UILogicalPoint delta) const noexcept
{
    if (!isLiveMultilineTextEdit(textEdit) || !isNodeEnabled(textEdit))
    {
        return false;
    }
    const u32 idx = textEdit.index();
    const float wheelStep = textEditMultilineByNodeIndex[idx].wheelStep;
    const float current = textEditScrollYByNodeIndex[idx];
    const float maxScroll = textEditVisualLayoutsByNodeIndex[idx].maximumScrollY;
    const float step = std::isfinite(wheelStep) && wheelStep > 0.0F ? wheelStep : 24.0F;
    const float next = (std::clamp)(current - delta.y * step, 0.0F, maxScroll);
    return next != current;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyTextEditScrollWheel(UINodeId textEdit, UILogicalPoint delta)
{
    if (!isLiveMultilineTextEdit(textEdit) || !isNodeEnabled(textEdit))
    {
        return false;
    }
    const u32 idx = textEdit.index();
    const float wheelStep = textEditMultilineByNodeIndex[idx].wheelStep;
    const float current = textEditScrollYByNodeIndex[idx];
    const float maxScroll = textEditVisualLayoutsByNodeIndex[idx].maximumScrollY;
    const float step = std::isfinite(wheelStep) && wheelStep > 0.0F ? wheelStep : 24.0F;
    const float next = (std::clamp)(current - delta.y * step, 0.0F, maxScroll);
    if (next == current)
    {
        return false;
    }
    if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
    {
        return Core::failure(dirty.error());
    }
    textEditScrollYByNodeIndex[idx] = next;
    return true;
}

[[nodiscard]] ScrollBarGeometry UIContext::Impl::committedScrollBarGeometry(UINodeId scrollView, UIScrollAxes axis) const noexcept
{
    if (isLiveListView(scrollView))
    {
        if (axis != UIScrollAxes::Vertical)
        {
            return {};
        }
        const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
        return makeListViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                             state.paint.scrollBar);
    }
    if (isLiveTreeView(scrollView))
    {
        if (axis != UIScrollAxes::Vertical)
        {
            return {};
        }
        const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
        return makeTreeViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                             state.paint.scrollBar);
    }
    if (isLiveVirtualGridView(scrollView))
    {
        if (axis != UIScrollAxes::Vertical)
        {
            return {};
        }
        const VirtualGridViewState* state =
            virtualGridViewStorage.tryView(scrollView);
        return state != nullptr
                   ? makeVirtualGridViewScrollBarGeometry(
                         state->committedMetrics,
                         state->committedViewportRect,
                         state->paint.scrollBar)
                   : ScrollBarGeometry{};
    }
    if (isLiveDataGrid(scrollView))
    {
        const DataGridState* state = dataGridStorage.tryGrid(scrollView);
        if (state == nullptr)
        {
            return {};
        }
        const auto geometry = makeDataGridScrollBarGeometry(
            state->committedMetrics, state->committedBodyViewportRect,
            state->paint.scrollBar);
        return axis == UIScrollAxes::Horizontal ? geometry.horizontal
                                                : geometry.vertical;
    }
    if (!isLiveScrollView(scrollView))
    {
        return {};
    }
    const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
    if (state == nullptr)
    {
        return {};
    }
    return makeScrollBarGeometry(state->committedMetrics, state->committedViewportRect,
                                 scrollViewPaintsByNodeIndex[scrollView.index()], axis);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyScrollOffsetFromInput(UINodeId scrollView, UIScrollOffset requested)
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
    {
        return false;
    }
    if (isLiveListView(scrollView))
    {
        ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
        requested.x = 0.0F;
        requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
        if (state.requestedScrollOffset == requested.y)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.requestedScrollOffset = requested.y;
        return true;
    }
    if (isLiveTreeView(scrollView))
    {
        TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
        requested.x = 0.0F;
        requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
        if (state.requestedScrollOffset == requested.y)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.requestedScrollOffset = requested.y;
        return true;
    }
    if (isLiveVirtualGridView(scrollView))
    {
        VirtualGridViewState* state =
            virtualGridViewStorage.tryView(scrollView);
        if (state == nullptr)
        {
            return false;
        }
        requested.x = 0.0F;
        requested.y = normalizeFloat((std::clamp)(
            requested.y, 0.0F,
            state->committedMetrics.maxScrollOffset));
        if (state->requestedScrollOffset == requested.y)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state->requestedScrollOffset = requested.y;
        return true;
    }
    if (isLiveDataGrid(scrollView))
    {
        DataGridState* state = dataGridStorage.tryGrid(scrollView);
        if (state == nullptr)
        {
            return false;
        }
        requested.x = normalizeFloat((std::clamp)(
            requested.x, 0.0F,
            state->committedMetrics.maxScrollOffset.x));
        requested.y = normalizeFloat((std::clamp)(
            requested.y, 0.0F,
            state->committedMetrics.maxScrollOffset.y));
        if (state->requestedScrollOffset == requested)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state->requestedScrollOffset = requested;
        return true;
    }
    UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
    if (state == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI ScrollView is missing Scroll behavior state");
    }
    const UIScrollViewMetrics& metrics = state->committedMetrics;
    requested.x = hasScrollAxis(state->style.axes, UIScrollAxes::Horizontal)
                      ? normalizeFloat((std::clamp)(requested.x, 0.0F, metrics.maxOffsetX()))
                      : 0.0F;
    requested.y = hasScrollAxis(state->style.axes, UIScrollAxes::Vertical)
                      ? normalizeFloat((std::clamp)(requested.y, 0.0F, metrics.maxOffsetY()))
                      : 0.0F;
    if (state->requestedOffset == requested)
    {
        return false;
    }
    if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
    {
        return Core::failure(dirty.error());
    }
    state->requestedOffset = requested;
    return true;
}

[[nodiscard]] UIScrollOffset UIContext::Impl::resolvedScrollWheelOffset(UINodeId scrollView, UILogicalPoint delta) const noexcept
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
    {
        return {};
    }
    if (isLiveListView(scrollView))
    {
        const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
        return UIScrollOffset{
            .x = 0.0F,
            .y = resolveVirtualScrollWheelOffset(
                state.requestedScrollOffset,
                state.committedMetrics.maxScrollOffset,
                state.style.wheelStep, delta),
        };
    }
    if (isLiveTreeView(scrollView))
    {
        const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
        return UIScrollOffset{
            .x = 0.0F,
            .y = resolveVirtualScrollWheelOffset(
                state.requestedScrollOffset,
                state.committedMetrics.maxScrollOffset,
                state.style.wheelStep, delta),
            };
    }
    if (isLiveVirtualGridView(scrollView))
    {
        const VirtualGridViewState* state =
            virtualGridViewStorage.tryView(scrollView);
        return state != nullptr
                   ? UIScrollOffset{
                         .x = 0.0F,
                         .y = resolveVirtualScrollWheelOffset(
                             state->requestedScrollOffset,
                             state->committedMetrics.maxScrollOffset,
                             state->style.wheelStep, delta),
                     }
                   : UIScrollOffset{};
    }
    if (isLiveDataGrid(scrollView))
    {
        const DataGridState* state = dataGridStorage.tryGrid(scrollView);
        return state != nullptr
                   ? resolveScrollWheelOffset(
                         state->requestedScrollOffset,
                         UIScrollViewStyle{
                             .axes = UIScrollAxes::Both,
                             .scrollBarVisibility =
                                 state->style.scrollBarVisibility,
                             .wheelStep = state->style.wheelStep,
                         },
                         makeDataGridScrollMetrics(
                             state->committedMetrics),
                         delta)
                   : UIScrollOffset{};
    }
    const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
    if (state == nullptr)
    {
        return {};
    }
    return resolveScrollWheelOffset(
        state->requestedOffset, state->style, state->committedMetrics, delta);
}

[[nodiscard]] bool UIContext::Impl::scrollWheelWouldChange(UINodeId scrollView, UILogicalPoint delta) const noexcept
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
    {
        return false;
    }
    const UIScrollOffset resolved = resolvedScrollWheelOffset(scrollView, delta);
    if (isLiveListView(scrollView))
    {
        return listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
    }
    if (isLiveTreeView(scrollView))
    {
        return treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
    }
    if (isLiveVirtualGridView(scrollView))
    {
        const VirtualGridViewState* state =
            virtualGridViewStorage.tryView(scrollView);
        return state != nullptr &&
               state->requestedScrollOffset != resolved.y;
    }
    if (isLiveDataGrid(scrollView))
    {
        const DataGridState* state = dataGridStorage.tryGrid(scrollView);
        return state != nullptr && state->requestedScrollOffset != resolved;
    }
    const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
    return state != nullptr && state->requestedOffset != resolved;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyScrollWheel(UINodeId scrollView, UILogicalPoint delta)
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
    {
        return false;
    }
    const UIScrollOffset next = resolvedScrollWheelOffset(scrollView, delta);
    return applyScrollOffsetFromInput(scrollView, next);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyScrollThumbFromPointer(UINodeId scrollView, UIScrollAxes axis,
                                                            UILogicalPoint position, float grabOffset)
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
        (isLiveVerticalVirtualView(scrollView) &&
         axis != UIScrollAxes::Vertical))
    {
        return false;
    }
    const bool listView = isLiveListView(scrollView);
    const bool treeView = isLiveTreeView(scrollView);
    const bool virtualGridView = isLiveVirtualGridView(scrollView);
    const bool dataGrid = isLiveDataGrid(scrollView);
    const UIScrollBehaviorState* scrollState =
        listView || treeView || virtualGridView || dataGrid
            ? nullptr
            : behaviorStateStorage.tryScrollState(scrollView.index());
    if (!listView && !treeView && !virtualGridView && !dataGrid &&
        scrollState == nullptr)
    {
        return false;
    }
    const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
    const float maxOffset = listView
                                ? listViewStatesByNodeIndex[scrollView.index()]
                                      .committedMetrics.maxScrollOffset
                            : treeView
                                ? treeViewStatesByNodeIndex[scrollView.index()]
                                      .committedMetrics.maxScrollOffset
                            : virtualGridView
                                ? virtualGridViewStorage.tryView(scrollView)
                                      ->committedMetrics.maxScrollOffset
                            : dataGrid
                                ? (axis == UIScrollAxes::Horizontal
                                       ? dataGridStorage.tryGrid(scrollView)
                                             ->committedMetrics.maxScrollOffset.x
                                       : dataGridStorage.tryGrid(scrollView)
                                             ->committedMetrics.maxScrollOffset.y)
                                : scrollAxisMaxOffset(
                                      scrollState->committedMetrics, axis);
    const auto axisOffset = resolveScrollThumbOffset(
        geometry, axis, position, grabOffset, maxOffset);
    if (!axisOffset)
    {
        return false;
    }
    UIScrollOffset next = listView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = listViewStatesByNodeIndex[scrollView.index()]
                                             .requestedScrollOffset,
                                }
                          : treeView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = treeViewStatesByNodeIndex[scrollView.index()]
                                             .requestedScrollOffset,
                                }
                          : virtualGridView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = virtualGridViewStorage.tryView(scrollView)
                                             ->requestedScrollOffset,
                                }
                          : dataGrid
                              ? dataGridStorage.tryGrid(scrollView)
                                    ->requestedScrollOffset
                              : scrollState->requestedOffset;
    setScrollAxisOffset(next, axis, *axisOffset);
    return applyScrollOffsetFromInput(scrollView, next);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::applyScrollTrackPage(UINodeId scrollView, UIScrollAxes axis,
                                                     UILogicalPoint position)
{
    if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
        (isLiveVerticalVirtualView(scrollView) &&
         axis != UIScrollAxes::Vertical))
    {
        return false;
    }
    const bool listView = isLiveListView(scrollView);
    const bool treeView = isLiveTreeView(scrollView);
    const bool virtualGridView = isLiveVirtualGridView(scrollView);
    const bool dataGrid = isLiveDataGrid(scrollView);
    const UIScrollBehaviorState* scrollState =
        listView || treeView || virtualGridView || dataGrid
            ? nullptr
            : behaviorStateStorage.tryScrollState(scrollView.index());
    if (!listView && !treeView && !virtualGridView && !dataGrid &&
        scrollState == nullptr)
    {
        return false;
    }
    const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
    if (!geometry.visible)
    {
        return false;
    }
    const float pageExtent = listView
                                 ? listViewStatesByNodeIndex[scrollView.index()]
                                       .committedMetrics.viewportSize.height
                             : treeView
                                 ? treeViewStatesByNodeIndex[scrollView.index()]
                                       .committedMetrics.viewportSize.height
                             : virtualGridView
                                 ? virtualGridViewStorage.tryView(scrollView)
                                       ->committedMetrics.viewportSize.height
                             : dataGrid
                                 ? (axis == UIScrollAxes::Horizontal
                                        ? dataGridStorage.tryGrid(scrollView)
                                              ->committedMetrics.viewportSize.width
                                        : dataGridStorage.tryGrid(scrollView)
                                              ->committedMetrics.viewportSize.height)
                                 : (axis == UIScrollAxes::Horizontal
                                        ? scrollState->committedMetrics.viewportSize.width
                                        : scrollState->committedMetrics.viewportSize.height);
    UIScrollOffset next = listView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = listViewStatesByNodeIndex[scrollView.index()]
                                             .requestedScrollOffset,
                                }
                          : treeView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = treeViewStatesByNodeIndex[scrollView.index()]
                                             .requestedScrollOffset,
                                }
                          : virtualGridView
                              ? UIScrollOffset{
                                    .x = 0.0F,
                                    .y = virtualGridViewStorage.tryView(scrollView)
                                             ->requestedScrollOffset,
                                }
                          : dataGrid
                              ? dataGridStorage.tryGrid(scrollView)
                                    ->requestedScrollOffset
                              : scrollState->requestedOffset;
    const auto axisOffset = resolveScrollTrackPageOffset(
        geometry, axis, position, scrollAxisOffset(next, axis), pageExtent);
    if (!axisOffset)
    {
        return false;
    }
    setScrollAxisOffset(next, axis, *axisOffset);
    return applyScrollOffsetFromInput(scrollView, next);
}

[[nodiscard]] ScrollBarPointerHit UIContext::Impl::scrollBarPointerHit(std::span<const u32> routePath,
                                                     std::span<const UICommittedHitEntry> entries,
                                                     UILogicalPoint position) const noexcept
{
    for (const u32 routeEntryIndex : routePath)
    {
        if (routeEntryIndex >= entries.size())
        {
            continue;
        }
        const UINodeId node = entries[routeEntryIndex].node;
        if (!isLiveScrollable(node) || !isNodeEnabled(node))
        {
            continue;
        }
        if (isLiveVerticalVirtualView(node))
        {
            const VirtualGridViewState* virtualGrid =
                virtualGridViewStorage.tryView(node);
            const float maxOffset =
                isLiveListView(node)
                    ? listViewStatesByNodeIndex[node.index()]
                          .committedMetrics.maxScrollOffset
                : isLiveTreeView(node)
                    ? treeViewStatesByNodeIndex[node.index()]
                          .committedMetrics.maxScrollOffset
                : virtualGrid != nullptr
                    ? virtualGrid->committedMetrics.maxScrollOffset
                    : 0.0F;
            if (!(maxOffset > 0.0F))
            {
                continue;
            }
            const ScrollBarGeometry geometry = committedScrollBarGeometry(node, UIScrollAxes::Vertical);
            if (geometry.visible && containsPointHalfOpen(geometry.track, position))
            {
                return ScrollBarPointerHit{
                    .scrollView = node,
                    .axis = UIScrollAxes::Vertical,
                    .geometry = geometry,
                    .thumb = containsPointHalfOpen(geometry.thumb, position),
                };
            }
            continue;
        }
        if (isLiveDataGrid(node))
        {
            const DataGridState* state = dataGridStorage.tryGrid(node);
            if (state == nullptr)
            {
                continue;
            }
            for (const UIScrollAxes axis : {
                     UIScrollAxes::Horizontal,
                     UIScrollAxes::Vertical,
                 })
            {
                const float maxOffset =
                    axis == UIScrollAxes::Horizontal
                        ? state->committedMetrics.maxScrollOffset.x
                        : state->committedMetrics.maxScrollOffset.y;
                if (!(maxOffset > 0.0F))
                {
                    continue;
                }
                const ScrollBarGeometry geometry =
                    committedScrollBarGeometry(node, axis);
                if (!geometry.visible ||
                    !containsPointHalfOpen(geometry.track, position))
                {
                    continue;
                }
                return ScrollBarPointerHit{
                    .scrollView = node,
                    .axis = axis,
                    .geometry = geometry,
                    .thumb = containsPointHalfOpen(
                        geometry.thumb, position),
                };
            }
            continue;
        }
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(node.index());
        if (state == nullptr)
        {
            continue;
        }
        for (const UIScrollAxes axis : {UIScrollAxes::Horizontal, UIScrollAxes::Vertical})
        {
            if (!hasScrollAxis(state->style.axes, axis) ||
                !(scrollAxisMaxOffset(state->committedMetrics, axis) > 0.0F))
            {
                continue;
            }
            const ScrollBarGeometry geometry = committedScrollBarGeometry(node, axis);
            if (!geometry.visible || !containsPointHalfOpen(geometry.track, position))
            {
                continue;
            }
            return ScrollBarPointerHit{
                .scrollView = node,
                .axis = axis,
                .geometry = geometry,
                .thumb = containsPointHalfOpen(geometry.thumb, position),
            };
        }
    }
    return {};
}

[[nodiscard]] Core::Status UIContext::Impl::setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                        const UIScrollViewStyle& style)
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
    auto scrollResult = resolveScroll(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    auto normalized = Detail::normalizeScrollViewStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    UIScrollBehaviorState& state = *behaviorStateStorage.tryScrollState(scrollView.index());
    if (state.style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(scrollView); !dirty)
    {
        return dirty;
    }
    state.style = *normalized;
    return Core::success();
}

[[nodiscard]] Core::Result<UIScrollViewStyle> UIContext::Impl::scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId scrollView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    return behaviorStateStorage.tryScrollState(scrollView.index())->style;
}

[[nodiscard]] Core::Status UIContext::Impl::setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                         UIScrollOffset offset)
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
    auto scrollResult = resolveScroll(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    auto normalized = Detail::normalizeScrollOffset(offset);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    UIScrollBehaviorState& state = *behaviorStateStorage.tryScrollState(scrollView.index());
    if (state.requestedOffset == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
    {
        return dirty;
    }
    state.requestedOffset = *normalized;
    return Core::success();
}

[[nodiscard]] Core::Result<UIScrollOffset> UIContext::Impl::scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                       UINodeId scrollView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    return behaviorStateStorage.tryScrollState(scrollView.index())->requestedOffset;
}

[[nodiscard]] Core::Result<UIScrollViewMetrics> UIContext::Impl::scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    return behaviorStateStorage.tryScrollState(scrollView.index())->committedMetrics;
}

[[nodiscard]] Core::Status UIContext::Impl::setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                        const UIScrollViewPaint& paint)
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
    auto scrollResult = resolveScrollView(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    auto normalized = Detail::normalizeScrollViewPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    UIScrollViewPaint& state = scrollViewPaintsByNodeIndex[scrollView.index()];
    if (state == *normalized)
    {
        detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
        return Core::success();
    }
    const bool layoutChanged = state.thickness != normalized->thickness ||
                               state.minThumbExtent != normalized->minThumbExtent;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(scrollView) : markPaintDirty(scrollView);
    if (!dirty)
    {
        return dirty;
    }
    state = *normalized;
    detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIScrollViewPaint> UIContext::Impl::scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId scrollView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    return scrollViewPaintsByNodeIndex[scrollView.index()];
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                                 UINodeId scrollView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
    if (!scrollResult)
    {
        return Core::failure(scrollResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, scrollView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
    }
    return scrollThumbDragActive && armedScrollView == scrollView;
}

} // namespace Tina::UI
