#include "UIMenuStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UIMenuStateStorage::UIMenuStateStorage(usize nodeCapacity,
                                       std::pmr::memory_resource& resource)
    : menusByNodeIndex_(&resource), itemsByNodeIndex_(&resource),
      menuForAnchorByNodeIndex_(&resource), layoutScratchByNodeIndex_(&resource)
{
    menusByNodeIndex_.resize(nodeCapacity);
    itemsByNodeIndex_.resize(nodeCapacity);
    menuForAnchorByNodeIndex_.resize(nodeCapacity);
    layoutScratchByNodeIndex_.resize(nodeCapacity);
}

usize UIMenuStateStorage::capacity() const noexcept
{
    return menusByNodeIndex_.size();
}

bool UIMenuStateStorage::containsMenu(UINodeId menu) const noexcept
{
    return menu.hasValue() && menu.index() < menusByNodeIndex_.size() &&
           menusByNodeIndex_[menu.index()].node == menu;
}

bool UIMenuStateStorage::containsMenuItem(UINodeId item) const noexcept
{
    return item.hasValue() && item.index() < itemsByNodeIndex_.size() &&
           itemsByNodeIndex_[item.index()].node == item;
}

MenuState* UIMenuStateStorage::tryMenu(UINodeId menu) noexcept
{
    return const_cast<MenuState*>(std::as_const(*this).tryMenu(menu));
}

const MenuState* UIMenuStateStorage::tryMenu(UINodeId menu) const noexcept
{
    return containsMenu(menu) ? &menusByNodeIndex_[menu.index()] : nullptr;
}

MenuItemState* UIMenuStateStorage::tryItem(UINodeId item) noexcept
{
    return const_cast<MenuItemState*>(std::as_const(*this).tryItem(item));
}

const MenuItemState* UIMenuStateStorage::tryItem(UINodeId item) const noexcept
{
    return containsMenuItem(item) ? &itemsByNodeIndex_[item.index()] : nullptr;
}

MenuLayoutScratch& UIMenuStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    assert(nodeIndex < layoutScratchByNodeIndex_.size());
    return layoutScratchByNodeIndex_[nodeIndex];
}

void UIMenuStateStorage::initializeMenu(UINodeId menu,
                                       const UIMenuConfig& config) noexcept
{
    assert(menu.hasValue() && menu.index() < menusByNodeIndex_.size());
    resetNode(menu.index());
    menusByNodeIndex_[menu.index()] = MenuState{.node = menu, .config = config};
}

void UIMenuStateStorage::initializeMenuItem(
    UINodeId item, UINodeId menu, const UIMenuItemConfig& config) noexcept
{
    assert(item.hasValue() && item.index() < itemsByNodeIndex_.size());
    assert(containsMenu(menu));
    resetNode(item.index());
    itemsByNodeIndex_[item.index()] = MenuItemState{
        .node = item,
        .config = config,
        .menu = menu,
        .checked = config.checked,
    };
}

void UIMenuStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= menusByNodeIndex_.size())
    {
        return;
    }
    const UINodeId menu = menusByNodeIndex_[nodeIndex].node;
    if (menu.hasValue())
    {
        static_cast<void>(unlinkMenu(menu));
        if (activeMenu_ == menu)
        {
            activeMenu_ = {};
        }
    }
    const UINodeId anchoredMenu = menuForAnchorByNodeIndex_[nodeIndex];
    if (containsMenu(anchoredMenu))
    {
        menusByNodeIndex_[anchoredMenu.index()].anchor = {};
        menusByNodeIndex_[anchoredMenu.index()].open = false;
        if (activeMenu_ == anchoredMenu)
        {
            activeMenu_ = {};
        }
    }
    menuForAnchorByNodeIndex_[nodeIndex] = {};
    menusByNodeIndex_[nodeIndex] = {};
    itemsByNodeIndex_[nodeIndex] = {};
    layoutScratchByNodeIndex_[nodeIndex] = {};
}

bool UIMenuStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue() || node.index() >= menusByNodeIndex_.size())
    {
        return false;
    }
    const bool related = containsMenu(node) || containsMenuItem(node) ||
                         menuForAnchor(node).hasValue();
    resetNode(node.index());
    return related;
}

bool UIMenuStateStorage::hasRelationship(UINodeId menu,
                                        UINodeId anchor) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && anchor.hasValue() &&
           anchor.index() < menuForAnchorByNodeIndex_.size() &&
           state->anchor == anchor &&
           menuForAnchorByNodeIndex_[anchor.index()] == menu;
}

UINodeId UIMenuStateStorage::menuForAnchor(UINodeId anchor) const noexcept
{
    if (!anchor.hasValue() || anchor.index() >= menuForAnchorByNodeIndex_.size())
    {
        return {};
    }
    const UINodeId menu = menuForAnchorByNodeIndex_[anchor.index()];
    return hasRelationship(menu, anchor) ? menu : UINodeId{};
}

UINodeId UIMenuStateStorage::anchorForMenu(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && hasRelationship(menu, state->anchor)
               ? state->anchor
               : UINodeId{};
}

void UIMenuStateStorage::linkAnchorValidated(UINodeId menu,
                                            UINodeId anchor) noexcept
{
    assert(containsMenu(menu));
    assert(anchor.hasValue() && anchor.index() < menuForAnchorByNodeIndex_.size());
    static_cast<void>(unlinkMenu(menu));
    static_cast<void>(unlinkAnchor(anchor));
    menusByNodeIndex_[menu.index()].anchor = anchor;
    menuForAnchorByNodeIndex_[anchor.index()] = menu;
}

UINodeId UIMenuStateStorage::unlinkMenu(UINodeId menu) noexcept
{
    MenuState* state = tryMenu(menu);
    if (state == nullptr)
    {
        return {};
    }
    const UINodeId anchor = state->anchor;
    if (anchor.hasValue() && anchor.index() < menuForAnchorByNodeIndex_.size() &&
        menuForAnchorByNodeIndex_[anchor.index()] == menu)
    {
        menuForAnchorByNodeIndex_[anchor.index()] = {};
    }
    state->anchor = {};
    state->open = false;
    if (activeMenu_ == menu)
    {
        activeMenu_ = {};
    }
    return anchor;
}

UINodeId UIMenuStateStorage::unlinkAnchor(UINodeId anchor) noexcept
{
    if (!anchor.hasValue() || anchor.index() >= menuForAnchorByNodeIndex_.size())
    {
        return {};
    }
    const UINodeId menu = menuForAnchorByNodeIndex_[anchor.index()];
    menuForAnchorByNodeIndex_[anchor.index()] = {};
    if (MenuState* state = tryMenu(menu); state != nullptr && state->anchor == anchor)
    {
        state->anchor = {};
        state->open = false;
    }
    if (activeMenu_ == menu)
    {
        activeMenu_ = {};
    }
    return menu;
}

UINodeId UIMenuStateStorage::menuForItem(UINodeId item) const noexcept
{
    const MenuItemState* state = tryItem(item);
    return state != nullptr && containsMenu(state->menu) ? state->menu : UINodeId{};
}

UINodeId UIMenuStateStorage::activeMenu() const noexcept
{
    const MenuState* state = tryMenu(activeMenu_);
    return state != nullptr && state->open ? activeMenu_ : UINodeId{};
}

UINodeId UIMenuStateStorage::openValidated(UINodeId menu) noexcept
{
    assert(containsMenu(menu));
    const UINodeId previous = activeMenu();
    if (previous.hasValue() && previous != menu)
    {
        menusByNodeIndex_[previous.index()].open = false;
    }
    menusByNodeIndex_[menu.index()].open = true;
    activeMenu_ = menu;
    return previous != menu ? previous : UINodeId{};
}

bool UIMenuStateStorage::close(UINodeId menu) noexcept
{
    MenuState* state = tryMenu(menu);
    if (state == nullptr)
    {
        return false;
    }
    const bool changed = state->open || activeMenu_ == menu;
    state->open = false;
    if (activeMenu_ == menu)
    {
        activeMenu_ = {};
    }
    return changed;
}

bool UIMenuStateStorage::itemChecked(UINodeId item) const noexcept
{
    const MenuItemState* state = tryItem(item);
    return state != nullptr && state->checked;
}

bool UIMenuStateStorage::hasCheckedRadioPeer(UINodeId item) const noexcept
{
    const MenuItemState* state = tryItem(item);
    if (state == nullptr || state->config.kind != UIMenuItemKind::Radio)
    {
        return false;
    }
    for (const MenuItemState& peer : itemsByNodeIndex_)
    {
        if (peer.node != item && peer.menu == state->menu && peer.checked &&
            peer.config.kind == UIMenuItemKind::Radio &&
            peer.config.radioGroup == state->config.radioGroup)
        {
            return true;
        }
    }
    return false;
}

bool UIMenuStateStorage::setItemChecked(UINodeId item, bool checked) noexcept
{
    MenuItemState* state = tryItem(item);
    if (state == nullptr || state->checked == checked)
    {
        return false;
    }
    state->checked = checked;
    return true;
}

void UIMenuStateStorage::publishMetrics(u32 menuIndex) noexcept
{
    assert(menuIndex < menusByNodeIndex_.size());
    menusByNodeIndex_[menuIndex].committedMetrics =
        layoutScratchByNodeIndex_[menuIndex].metrics;
}

UIMenuMetrics UIMenuStateStorage::committedMetrics(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr ? state->committedMetrics : UIMenuMetrics{};
}

} // namespace Tina::UI::Detail
