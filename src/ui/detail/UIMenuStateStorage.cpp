#include "UIMenuStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UIMenuStateStorage::UIMenuStateStorage(usize menuCapacity,
                                       usize menuItemCapacity,
                                       std::pmr::memory_resource& resource)
    : menus_(menuCapacity, resource), items_(menuItemCapacity, resource)
{}

usize UIMenuStateStorage::capacity() const noexcept
{
    return menus_.capacity();
}

usize UIMenuStateStorage::activeMenuCount() const noexcept
{
    return menus_.size();
}

usize UIMenuStateStorage::activeMenuItemCount() const noexcept
{
    return items_.size();
}

usize UIMenuStateStorage::availableMenuCount() const noexcept
{
    return menus_.availableCount();
}

usize UIMenuStateStorage::availableMenuItemCount() const noexcept
{
    return items_.availableCount();
}

bool UIMenuStateStorage::containsMenu(UINodeId menu) const noexcept
{
    return menus_.contains(menu);
}

bool UIMenuStateStorage::containsMenuItem(UINodeId item) const noexcept
{
    return items_.contains(item);
}

MenuState* UIMenuStateStorage::tryMenu(UINodeId menu) noexcept
{
    return const_cast<MenuState*>(std::as_const(*this).tryMenu(menu));
}

const MenuState* UIMenuStateStorage::tryMenu(UINodeId menu) const noexcept
{
    return menus_.tryGet(menu);
}

MenuItemState* UIMenuStateStorage::tryItem(UINodeId item) noexcept
{
    return const_cast<MenuItemState*>(std::as_const(*this).tryItem(item));
}

const MenuItemState* UIMenuStateStorage::tryItem(UINodeId item) const noexcept
{
    return items_.tryGet(item);
}

MenuLayoutScratch& UIMenuStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    MenuState* state = menus_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->layoutScratch;
}

bool UIMenuStateStorage::initializeMenu(UINodeId menu,
                                       const UIMenuConfig& config) noexcept
{
    assert(menu.hasValue());
    resetNode(menu.index());
    return menus_.insertOrAssign(MenuState{.node = menu, .config = config});
}

bool UIMenuStateStorage::initializeMenuItem(
    UINodeId item, UINodeId menu, const UIMenuItemConfig& config) noexcept
{
    assert(item.hasValue());
    assert(containsMenu(menu));
    resetNode(item.index());
    return items_.insertOrAssign(MenuItemState{
        .node = item,
        .config = config,
        .menu = menu,
        .checked = config.checked,
    });
}

void UIMenuStateStorage::resetNode(u32 nodeIndex) noexcept
{
    const MenuState* menuState = menus_.tryGetByIndex(nodeIndex);
    const UINodeId menu = menuState != nullptr ? menuState->node : UINodeId{};
    if (menu.hasValue())
    {
        static_cast<void>(unlinkMenu(menu));
        static_cast<void>(unlinkSubmenuMenu(menu));
    }
    const MenuItemState* itemState = items_.tryGetByIndex(nodeIndex);
    const UINodeId item = itemState != nullptr ? itemState->node : UINodeId{};
    if (item.hasValue())
    {
        static_cast<void>(unlinkSubmenuItem(item));
    }
    for (MenuState& candidate : menus_.states())
    {
        if (candidate.anchor.hasValue() && candidate.anchor.index() == nodeIndex)
        {
            const UINodeId anchoredMenu = candidate.node;
            candidate.anchor = {};
            static_cast<void>(close(anchoredMenu));
            break;
        }
    }
    static_cast<void>(menus_.eraseByIndex(nodeIndex));
    static_cast<void>(items_.eraseByIndex(nodeIndex));
}

bool UIMenuStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue())
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
    return state != nullptr && anchor.hasValue() && state->anchor == anchor &&
           menuForAnchor(anchor) == menu;
}

UINodeId UIMenuStateStorage::menuForAnchor(UINodeId anchor) const noexcept
{
    if (!anchor.hasValue())
    {
        return {};
    }
    for (const MenuState& state : menus_.states())
    {
        if (state.anchor == anchor)
        {
            return state.node;
        }
    }
    return {};
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
    assert(anchor.hasValue());
    static_cast<void>(unlinkMenu(menu));
    static_cast<void>(unlinkAnchor(anchor));
    MenuState* state = tryMenu(menu);
    assert(state != nullptr);
    state->anchor = anchor;
    clearInvocationAnchorRect(menu);
}

UINodeId UIMenuStateStorage::unlinkMenu(UINodeId menu) noexcept
{
    MenuState* state = tryMenu(menu);
    if (state == nullptr)
    {
        return {};
    }
    const UINodeId anchor = state->anchor;
    state->anchor = {};
    static_cast<void>(close(menu));
    return anchor;
}

UINodeId UIMenuStateStorage::unlinkAnchor(UINodeId anchor) noexcept
{
    if (!anchor.hasValue())
    {
        return {};
    }
    const UINodeId menu = menuForAnchor(anchor);
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
    MenuItemState* itemState = tryItem(item);
    MenuState* menuState = tryMenu(submenu);
    assert(itemState != nullptr && menuState != nullptr);
    assert(itemState->config.kind == UIMenuItemKind::Submenu);
    assert(!anchorForMenu(submenu).hasValue());
    static_cast<void>(unlinkSubmenuItem(item));
    static_cast<void>(unlinkSubmenuMenu(submenu));
    itemState->submenu = submenu;
    menuState->parentItem = item;
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
    MenuState* state = tryMenu(menu);
    assert(state != nullptr);
    state->invocationAnchorRect = rect;
    state->hasInvocationAnchorRect = true;
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
    while (current.hasValue() && visited++ < menus_.capacity())
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
    while (current.hasValue() && visited++ < menus_.capacity())
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
           visited++ < menus_.capacity())
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
    MenuState* state = tryMenu(menu);
    assert(state != nullptr);
    state->open = true;
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
    MenuState* state = tryMenu(menu);
    assert(state != nullptr);
    state->open = true;
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
    while (current.hasValue() && visited++ < menus_.capacity())
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
    for (const MenuItemState& peer : items_.states())
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
    MenuState* state = menus_.tryGetByIndex(menuIndex);
    assert(state != nullptr);
    state->committedMetrics = state->layoutScratch.metrics;
}

UIMenuMetrics UIMenuStateStorage::committedMetrics(UINodeId menu) const noexcept
{
    const MenuState* state = tryMenu(menu);
    return state != nullptr ? state->committedMetrics : UIMenuMetrics{};
}

} // namespace Tina::UI::Detail
