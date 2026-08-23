#include "UITabViewStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UITabViewStateStorage::UITabViewStateStorage(
    usize tabViewCapacity, usize tabCapacity,
    std::pmr::memory_resource& resource)
    : tabViews_(tabViewCapacity, resource), tabs_(tabCapacity, resource)
{}

usize UITabViewStateStorage::capacity() const noexcept
{
    return tabViews_.capacity();
}

usize UITabViewStateStorage::availableTabViewCount() const noexcept
{
    return tabViews_.availableCount();
}

usize UITabViewStateStorage::availableTabCount() const noexcept
{
    return tabs_.availableCount();
}

bool UITabViewStateStorage::containsTabView(UINodeId tabView) const noexcept
{
    return tabViews_.contains(tabView);
}

bool UITabViewStateStorage::containsTab(UINodeId tab) const noexcept
{
    return tabs_.contains(tab);
}

TabViewState* UITabViewStateStorage::tryTabView(UINodeId tabView) noexcept
{
    return const_cast<TabViewState*>(std::as_const(*this).tryTabView(tabView));
}

const TabViewState* UITabViewStateStorage::tryTabView(UINodeId tabView) const noexcept
{
    return tabViews_.tryGet(tabView);
}

TabState* UITabViewStateStorage::tryTab(UINodeId tab) noexcept
{
    return const_cast<TabState*>(std::as_const(*this).tryTab(tab));
}

const TabState* UITabViewStateStorage::tryTab(UINodeId tab) const noexcept
{
    return tabs_.tryGet(tab);
}

UITabPaint& UITabViewStateStorage::tabPaintByIndex(u32 nodeIndex) noexcept
{
    TabState* state = tabs_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->paint;
}

const UITabPaint& UITabViewStateStorage::tabPaintByIndex(u32 nodeIndex) const noexcept
{
    const TabState* state = tabs_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->paint;
}

TabViewLayoutScratch& UITabViewStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    TabViewState* state = tabViews_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->layoutScratch;
}

bool UITabViewStateStorage::initializeTabView(
    UINodeId tabView, const UITabViewConfig& config) noexcept
{
    assert(tabView.hasValue());
    resetNode(tabView.index());
    return tabViews_.insertOrAssign(TabViewState{
        .node = tabView,
        .config = config,
    });
}

bool UITabViewStateStorage::initializeTab(UINodeId tab, const UITabConfig& config) noexcept
{
    assert(tab.hasValue());
    resetNode(tab.index());
    return tabs_.insertOrAssign(TabState{
        .node = tab,
        .config = config,
    });
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
        const auto panelOwner = std::find_if(
            tabs_.states().begin(), tabs_.states().end(),
            [panel = tab != nullptr ? tab->panel : UINodeId{}](
                const TabState& candidate) noexcept {
                return panel.hasValue() && candidate.panel == panel;
            });
        if (tab == nullptr || tab->tabView != tabView || !tab->panel.hasValue() ||
            tab->previousTab != previous || tab->ordinal != ordinal ||
            panelOwner == tabs_.states().end() || panelOwner->node != current)
        {
            return false;
        }
        previous = current;
        current = tab->nextTab;
    }
    return previous == view->lastTab && !current.hasValue() &&
           tryTab(view->activeTab) != nullptr &&
           tryTab(view->activeTab)->tabView == tabView;
}

void UITabViewStateStorage::linkValidated(
    UINodeId tabView, std::span<const UITabViewItem> items, u32 activeIndex) noexcept
{
    assert(containsTabView(tabView));
    assert(!items.empty() && activeIndex < items.size());
    unlinkTabView(tabView);

    TabViewState* view = tryTabView(tabView);
    assert(view != nullptr);
    view->itemCount = static_cast<u32>(items.size());
    view->firstTab = items.front().tab;
    view->lastTab = items.back().tab;
    view->activeTab = items[activeIndex].tab;
    for (u32 index = 0; index < view->itemCount; ++index)
    {
        const UITabViewItem item = items[index];
        assert(containsTab(item.tab));
        TabState* tab = tryTab(item.tab);
        assert(tab != nullptr);
        tab->tabView = tabView;
        tab->panel = item.panel;
        tab->previousTab = index == 0 ? UINodeId{} : items[index - 1].tab;
        tab->nextTab = index + 1 == view->itemCount ? UINodeId{} : items[index + 1].tab;
        tab->ordinal = index;
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
    if (!panel.hasValue())
    {
        return {};
    }
    for (const TabState& state : tabs_.states())
    {
        if (state.panel == panel && relationshipValid(state.tabView))
        {
            return state.node;
        }
    }
    return {};
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
    if (TabViewState* view = tabViews_.tryGetByIndex(nodeIndex); view != nullptr)
    {
        unlinkTabView(view->node);
    }
    if (TabState* tab = tabs_.tryGetByIndex(nodeIndex); tab != nullptr)
    {
        const UINodeId owner = tab->tabView;
        if (containsTabView(owner))
        {
            unlinkTabView(owner);
        }
    }
    for (const TabState& state : tabs_.states())
    {
        if (state.panel.hasValue() && state.panel.index() == nodeIndex &&
            containsTabView(state.tabView))
        {
            unlinkTabView(state.tabView);
            break;
        }
    }
    static_cast<void>(tabViews_.eraseByIndex(nodeIndex));
    static_cast<void>(tabs_.eraseByIndex(nodeIndex));
}

bool UITabViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue())
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
    TabViewState* state = tabViews_.tryGetByIndex(tabViewIndex);
    assert(state != nullptr);
    state->committedMetrics = state->layoutScratch.metrics;
}

UITabViewMetrics UITabViewStateStorage::committedMetrics(UINodeId tabView) const noexcept
{
    const TabViewState* state = tryTabView(tabView);
    return state != nullptr ? state->committedMetrics : UITabViewMetrics{};
}

} // namespace Tina::UI::Detail
