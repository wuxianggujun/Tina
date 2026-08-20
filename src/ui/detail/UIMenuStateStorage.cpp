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
        static_cast<void>(unlinkSubmenuMenu(menu));
    }
    const UINodeId item = itemsByNodeIndex_[nodeIndex].node;
    if (item.hasValue())
    {
        static_cast<void>(unlinkSubmenuItem(item));
    }
    const UINodeId anchoredMenu = menuForAnchorByNodeIndex_[nodeIndex];
    if (containsMenu(anchoredMenu))
    {
        menusByNodeIndex_[anchoredMenu.index()].anchor = {};
        static_cast<void>(close(anchoredMenu));
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
                         menuForAnchor(node).hasValue() ||
                         submenuForItem(node).hasValue();
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
    assert(!parentItemForMenu(menu).hasValue());
    assert(anchor.hasValue() && anchor.index() < menuForAnchorByNodeIndex_.size());
    static_cast<void>(unlinkMenu(menu));
    static_cast<void>(unlinkAnchor(anchor));
    menusByNodeIndex_[menu.index()].anchor = anchor;
    clearInvocationAnchorRect(menu);
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
    static_cast<void>(close(menu));
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
        static_cast<void>(close(menu));
    }
    return menu;
}

bool UIMenuStateStorage::hasSubmenuRelationship(
    UINodeId item, UINodeId submenu) const noexcept
{
    const MenuItemState* itemState = tryItem(item);
    const MenuState* menuState = tryMenu(submenu);
    return itemState != nullptr && menuState != nullptr &&
           itemState->config.kind == UIMenuItemKind::Submenu &&
           itemState->submenu == submenu && menuState->parentItem == item;
}

UINodeId UIMenuStateStorage::submenuForItem(UINodeId item) const noexcept
{
    const MenuItemState* state = tryItem(item);
    return state != nullptr && hasSubmenuRelationship(item, state->submenu)
               ? state->submenu
               : UINodeId{};
}

UINodeId UIMenuStateStorage::parentItemForMenu(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && hasSubmenuRelationship(state->parentItem, menu)
               ? state->parentItem
               : UINodeId{};
}

UINodeId UIMenuStateStorage::parentMenu(UINodeId menu) const noexcept
{
    return menuForItem(parentItemForMenu(menu));
}

void UIMenuStateStorage::linkSubmenuValidated(
    UINodeId item, UINodeId submenu) noexcept
{
    assert(containsMenuItem(item));
    assert(containsMenu(submenu));
    assert(itemsByNodeIndex_[item.index()].config.kind == UIMenuItemKind::Submenu);
    assert(!anchorForMenu(submenu).hasValue());
    static_cast<void>(unlinkSubmenuItem(item));
    static_cast<void>(unlinkSubmenuMenu(submenu));
    itemsByNodeIndex_[item.index()].submenu = submenu;
    menusByNodeIndex_[submenu.index()].parentItem = item;
    clearInvocationAnchorRect(submenu);
}

UINodeId UIMenuStateStorage::unlinkSubmenuItem(UINodeId item) noexcept
{
    MenuItemState* itemState = tryItem(item);
    if (itemState == nullptr)
    {
        return {};
    }
    const UINodeId submenu = itemState->submenu;
    if (MenuState* menuState = tryMenu(submenu);
        menuState != nullptr && menuState->parentItem == item)
    {
        static_cast<void>(close(submenu));
        menuState->parentItem = {};
    }
    itemState->submenu = {};
    return submenu;
}

UINodeId UIMenuStateStorage::unlinkSubmenuMenu(UINodeId submenu) noexcept
{
    MenuState* menuState = tryMenu(submenu);
    if (menuState == nullptr)
    {
        return {};
    }
    const UINodeId item = menuState->parentItem;
    static_cast<void>(close(submenu));
    menuState->parentItem = {};
    if (MenuItemState* itemState = tryItem(item);
        itemState != nullptr && itemState->submenu == submenu)
    {
        itemState->submenu = {};
    }
    return item;
}

UINodeId UIMenuStateStorage::menuForItem(UINodeId item) const noexcept
{
    const MenuItemState* state = tryItem(item);
    return state != nullptr && containsMenu(state->menu) ? state->menu : UINodeId{};
}

bool UIMenuStateStorage::hasInvocationAnchorRect(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && state->hasInvocationAnchorRect;
}

UILogicalRect UIMenuStateStorage::invocationAnchorRect(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && state->hasInvocationAnchorRect
               ? state->invocationAnchorRect
               : UILogicalRect{};
}

void UIMenuStateStorage::setInvocationAnchorRectValidated(
    UINodeId menu, UILogicalRect rect) noexcept
{
    assert(containsMenu(menu));
    assert(!parentItemForMenu(menu).hasValue());
    MenuState& state = menusByNodeIndex_[menu.index()];
    state.invocationAnchorRect = rect;
    state.hasInvocationAnchorRect = true;
}

void UIMenuStateStorage::clearInvocationAnchorRect(UINodeId menu) noexcept
{
    if (MenuState* state = tryMenu(menu); state != nullptr)
    {
        state->invocationAnchorRect = {};
        state->hasInvocationAnchorRect = false;
    }
}

bool UIMenuStateStorage::isOpen(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr && state->open && isInActiveChain(menu);
}

bool UIMenuStateStorage::isInActiveChain(UINodeId menu) const noexcept
{
    if (!containsMenu(menu))
    {
        return false;
    }
    UINodeId current = activeMenu();
    usize visited = 0;
    while (current.hasValue() && visited++ < menusByNodeIndex_.size())
    {
        if (current == menu)
        {
            return true;
        }
        current = parentMenu(current);
    }
    return false;
}

UINodeId UIMenuStateStorage::rootMenu() const noexcept
{
    UINodeId current = activeMenu();
    UINodeId root = current;
    usize visited = 0;
    while (current.hasValue() && visited++ < menusByNodeIndex_.size())
    {
        const UINodeId parent = parentMenu(current);
        if (!parent.hasValue())
        {
            break;
        }
        root = parent;
        current = parent;
    }
    return root;
}

UINodeId UIMenuStateStorage::activeMenu() const noexcept
{
    const MenuState* state = tryMenu(activeMenu_);
    return state != nullptr && state->open ? activeMenu_ : UINodeId{};
}

UINodeId UIMenuStateStorage::activeChildMenu(UINodeId menu) const noexcept
{
    if (!isInActiveChain(menu) || activeMenu() == menu)
    {
        return {};
    }
    UINodeId child = activeMenu();
    UINodeId parent = parentMenu(child);
    usize visited = 0;
    while (parent.hasValue() && parent != menu &&
           visited++ < menusByNodeIndex_.size())
    {
        child = parent;
        parent = parentMenu(child);
    }
    return parent == menu ? child : UINodeId{};
}

UINodeId UIMenuStateStorage::openRootValidated(UINodeId menu) noexcept
{
    assert(containsMenu(menu));
    assert(!parentItemForMenu(menu).hasValue());
    const UINodeId previous = rootMenu();
    if (previous.hasValue() && previous != menu)
    {
        static_cast<void>(close(previous));
    } else if (const UINodeId activeChild = activeChildMenu(menu);
               activeChild.hasValue())
    {
        static_cast<void>(close(activeChild));
    }
    menusByNodeIndex_[menu.index()].open = true;
    activeMenu_ = menu;
    return previous != menu ? previous : UINodeId{};
}

UINodeId UIMenuStateStorage::openSubmenuValidated(UINodeId menu) noexcept
{
    assert(containsMenu(menu));
    clearInvocationAnchorRect(menu);
    const UINodeId parent = parentMenu(menu);
    assert(isOpen(parent));
    if (isInActiveChain(menu))
    {
        return {};
    }
    const UINodeId previousChild = activeChildMenu(parent);
    if (previousChild.hasValue())
    {
        static_cast<void>(close(previousChild));
    }
    menusByNodeIndex_[menu.index()].open = true;
    activeMenu_ = menu;
    return previousChild;
}

bool UIMenuStateStorage::close(UINodeId menu) noexcept
{
    MenuState* state = tryMenu(menu);
    if (state == nullptr)
    {
        return false;
    }
    const bool changed = state->open || isInActiveChain(menu);
    if (!isInActiveChain(menu))
    {
        state->open = false;
        clearInvocationAnchorRect(menu);
        return changed;
    }
    UINodeId current = activeMenu();
    UINodeId nextActive{};
    usize visited = 0;
    while (current.hasValue() && visited++ < menusByNodeIndex_.size())
    {
        MenuState* currentState = tryMenu(current);
        if (currentState == nullptr)
        {
            activeMenu_ = {};
            break;
        }
        currentState->open = false;
        currentState->invocationAnchorRect = {};
        currentState->hasInvocationAnchorRect = false;
        const UINodeId parent = parentMenu(current);
        if (current == menu)
        {
            const MenuState* parentState = tryMenu(parent);
            nextActive = parentState != nullptr && parentState->open
                             ? parent
                             : UINodeId{};
            break;
        }
        current = parent;
    }
    activeMenu_ = nextActive;
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
