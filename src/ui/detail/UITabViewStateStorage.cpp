#include "UITabViewStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UITabViewStateStorage::UITabViewStateStorage(
    usize nodeCapacity, std::pmr::memory_resource& resource)
    : tabViewsByNodeIndex_(&resource), tabsByNodeIndex_(&resource),
      tabForPanelByNodeIndex_(&resource), layoutScratchByNodeIndex_(&resource)
{
    tabViewsByNodeIndex_.resize(nodeCapacity);
    tabsByNodeIndex_.resize(nodeCapacity);
    tabForPanelByNodeIndex_.resize(nodeCapacity);
    layoutScratchByNodeIndex_.resize(nodeCapacity);
}

usize UITabViewStateStorage::capacity() const noexcept
{
    return tabViewsByNodeIndex_.size();
}

bool UITabViewStateStorage::containsTabView(UINodeId tabView) const noexcept
{
    return tabView.hasValue() && tabView.index() < tabViewsByNodeIndex_.size() &&
           tabViewsByNodeIndex_[tabView.index()].node == tabView;
}

bool UITabViewStateStorage::containsTab(UINodeId tab) const noexcept
{
    return tab.hasValue() && tab.index() < tabsByNodeIndex_.size() &&
           tabsByNodeIndex_[tab.index()].node == tab;
}

TabViewState* UITabViewStateStorage::tryTabView(UINodeId tabView) noexcept
{
    return const_cast<TabViewState*>(std::as_const(*this).tryTabView(tabView));
}

const TabViewState* UITabViewStateStorage::tryTabView(UINodeId tabView) const noexcept
{
    return containsTabView(tabView) ? &tabViewsByNodeIndex_[tabView.index()] : nullptr;
}

TabState* UITabViewStateStorage::tryTab(UINodeId tab) noexcept
{
    return const_cast<TabState*>(std::as_const(*this).tryTab(tab));
}

const TabState* UITabViewStateStorage::tryTab(UINodeId tab) const noexcept
{
    return containsTab(tab) ? &tabsByNodeIndex_[tab.index()] : nullptr;
}

UITabPaint& UITabViewStateStorage::tabPaintByIndex(u32 nodeIndex) noexcept
{
    assert(nodeIndex < tabsByNodeIndex_.size());
    return tabsByNodeIndex_[nodeIndex].paint;
}

const UITabPaint& UITabViewStateStorage::tabPaintByIndex(u32 nodeIndex) const noexcept
{
    assert(nodeIndex < tabsByNodeIndex_.size());
    return tabsByNodeIndex_[nodeIndex].paint;
}

TabViewLayoutScratch& UITabViewStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    assert(nodeIndex < layoutScratchByNodeIndex_.size());
    return layoutScratchByNodeIndex_[nodeIndex];
}

void UITabViewStateStorage::initializeTabView(
    UINodeId tabView, const UITabViewConfig& config) noexcept
{
    assert(tabView.hasValue() && tabView.index() < tabViewsByNodeIndex_.size());
    resetNode(tabView.index());
    tabViewsByNodeIndex_[tabView.index()] = TabViewState{
        .node = tabView,
        .config = config,
    };
}

void UITabViewStateStorage::initializeTab(UINodeId tab, const UITabConfig& config) noexcept
{
    assert(tab.hasValue() && tab.index() < tabsByNodeIndex_.size());
    resetNode(tab.index());
    tabsByNodeIndex_[tab.index()] = TabState{
        .node = tab,
        .config = config,
    };
}

bool UITabViewStateStorage::relationshipValid(UINodeId tabView) const noexcept
{
    const TabViewState* view = tryTabView(tabView);
    if (view == nullptr || view->itemCount == 0 || !view->firstTab.hasValue() ||
        !view->lastTab.hasValue() || !view->activeTab.hasValue())
    {
        return false;
    }
    UINodeId previous{};
    UINodeId current = view->firstTab;
    for (u32 ordinal = 0; ordinal < view->itemCount; ++ordinal)
    {
        const TabState* tab = tryTab(current);
        if (tab == nullptr || tab->tabView != tabView || !tab->panel.hasValue() ||
            tab->previousTab != previous || tab->ordinal != ordinal ||
            tab->panel.index() >= tabForPanelByNodeIndex_.size() ||
            tabForPanelByNodeIndex_[tab->panel.index()] != current)
        {
            return false;
        }
        previous = current;
        current = tab->nextTab;
    }
    return previous == view->lastTab && !current.hasValue() &&
           tryTab(view->activeTab) != nullptr &&
           tabsByNodeIndex_[view->activeTab.index()].tabView == tabView;
}

void UITabViewStateStorage::linkValidated(
    UINodeId tabView, std::span<const UITabViewItem> items, u32 activeIndex) noexcept
{
    assert(containsTabView(tabView));
    assert(!items.empty() && activeIndex < items.size());
    unlinkTabView(tabView);

    TabViewState& view = tabViewsByNodeIndex_[tabView.index()];
    view.itemCount = static_cast<u32>(items.size());
    view.firstTab = items.front().tab;
    view.lastTab = items.back().tab;
    view.activeTab = items[activeIndex].tab;
    for (u32 index = 0; index < view.itemCount; ++index)
    {
        const UITabViewItem item = items[index];
        assert(containsTab(item.tab));
        TabState& tab = tabsByNodeIndex_[item.tab.index()];
        tab.tabView = tabView;
        tab.panel = item.panel;
        tab.previousTab = index == 0 ? UINodeId{} : items[index - 1].tab;
        tab.nextTab = index + 1 == view.itemCount ? UINodeId{} : items[index + 1].tab;
        tab.ordinal = index;
        tabForPanelByNodeIndex_[item.panel.index()] = item.tab;
    }
}

void UITabViewStateStorage::unlinkTabView(UINodeId tabView) noexcept
{
    TabViewState* view = tryTabView(tabView);
    if (view == nullptr)
    {
        return;
    }
    UINodeId current = view->firstTab;
    for (u32 visited = 0; current.hasValue() && visited < view->itemCount; ++visited)
    {
        TabState* tab = tryTab(current);
        if (tab == nullptr || tab->tabView != tabView)
        {
            break;
        }
        const UINodeId next = tab->nextTab;
        if (tab->panel.hasValue() && tab->panel.index() < tabForPanelByNodeIndex_.size() &&
            tabForPanelByNodeIndex_[tab->panel.index()] == current)
        {
            tabForPanelByNodeIndex_[tab->panel.index()] = {};
        }
        tab->tabView = {};
        tab->panel = {};
        tab->previousTab = {};
        tab->nextTab = {};
        tab->ordinal = 0;
        current = next;
    }
    view->firstTab = {};
    view->lastTab = {};
    view->activeTab = {};
    view->itemCount = 0;
}

UINodeId UITabViewStateStorage::tabViewForTab(UINodeId tab) const noexcept
{
    const TabState* state = tryTab(tab);
    return state != nullptr && relationshipValid(state->tabView) ? state->tabView : UINodeId{};
}

UINodeId UITabViewStateStorage::tabForPanel(UINodeId panel) const noexcept
{
    if (!panel.hasValue() || panel.index() >= tabForPanelByNodeIndex_.size())
    {
        return {};
    }
    const UINodeId tab = tabForPanelByNodeIndex_[panel.index()];
    const TabState* state = tryTab(tab);
    return state != nullptr && state->panel == panel && relationshipValid(state->tabView)
               ? tab
               : UINodeId{};
}

UINodeId UITabViewStateStorage::tabViewForPanel(UINodeId panel) const noexcept
{
    return tabViewForTab(tabForPanel(panel));
}

UITabViewItem UITabViewStateStorage::itemAt(UINodeId tabView, u32 index) const noexcept
{
    const TabViewState* view = tryTabView(tabView);
    if (view == nullptr || index >= view->itemCount || !relationshipValid(tabView))
    {
        return {};
    }
    UINodeId current = view->firstTab;
    for (u32 ordinal = 0; ordinal < index; ++ordinal)
    {
        const TabState* tab = tryTab(current);
        current = tab != nullptr ? tab->nextTab : UINodeId{};
    }
    const TabState* tab = tryTab(current);
    return tab != nullptr ? UITabViewItem{.tab = current, .panel = tab->panel}
                          : UITabViewItem{};
}

u32 UITabViewStateStorage::itemCount(UINodeId tabView) const noexcept
{
    const TabViewState* view = tryTabView(tabView);
    return view != nullptr && relationshipValid(tabView) ? view->itemCount : 0;
}

UINodeId UITabViewStateStorage::activeTab(UINodeId tabView) const noexcept
{
    const TabViewState* view = tryTabView(tabView);
    return view != nullptr && relationshipValid(tabView) ? view->activeTab : UINodeId{};
}

UINodeId UITabViewStateStorage::activePanel(UINodeId tabView) const noexcept
{
    const TabState* tab = tryTab(activeTab(tabView));
    return tab != nullptr ? tab->panel : UINodeId{};
}

bool UITabViewStateStorage::setActiveTab(UINodeId tabView, UINodeId tab) noexcept
{
    TabViewState* view = tryTabView(tabView);
    const TabState* tabState = tryTab(tab);
    if (view == nullptr || tabState == nullptr || tabState->tabView != tabView ||
        !relationshipValid(tabView) || view->activeTab == tab)
    {
        return false;
    }
    view->activeTab = tab;
    return true;
}

void UITabViewStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= tabViewsByNodeIndex_.size())
    {
        return;
    }
    if (tabViewsByNodeIndex_[nodeIndex].node.hasValue())
    {
        unlinkTabView(tabViewsByNodeIndex_[nodeIndex].node);
    }
    if (tabsByNodeIndex_[nodeIndex].node.hasValue())
    {
        const UINodeId owner = tabsByNodeIndex_[nodeIndex].tabView;
        if (containsTabView(owner))
        {
            unlinkTabView(owner);
        }
    }
    const UINodeId panelTab = tabForPanelByNodeIndex_[nodeIndex];
    const TabState* panelTabState = tryTab(panelTab);
    if (panelTabState != nullptr && containsTabView(panelTabState->tabView))
    {
        unlinkTabView(panelTabState->tabView);
    }
    tabViewsByNodeIndex_[nodeIndex] = {};
    tabsByNodeIndex_[nodeIndex] = {};
    tabForPanelByNodeIndex_[nodeIndex] = {};
    layoutScratchByNodeIndex_[nodeIndex] = {};
}

bool UITabViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue() || node.index() >= tabViewsByNodeIndex_.size())
    {
        return false;
    }
    const bool related = containsTabView(node) || containsTab(node) ||
                         tabForPanel(node).hasValue();
    resetNode(node.index());
    return related;
}

void UITabViewStateStorage::publishMetrics(u32 tabViewIndex) noexcept
{
    assert(tabViewIndex < tabViewsByNodeIndex_.size());
    tabViewsByNodeIndex_[tabViewIndex].committedMetrics =
        layoutScratchByNodeIndex_[tabViewIndex].metrics;
}

UITabViewMetrics UITabViewStateStorage::committedMetrics(UINodeId tabView) const noexcept
{
    const TabViewState* state = tryTabView(tabView);
    return state != nullptr ? state->committedMetrics : UITabViewMetrics{};
}

} // namespace Tina::UI::Detail
