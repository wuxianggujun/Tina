#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIMenu.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>

namespace Tina::UI::Detail {

struct MenuLayoutScratch final {
    UIMenuMetrics metrics{};
};

struct MenuState final {
    UINodeId node{};
    UIMenuConfig config{};
    UINodeId anchor{};
    UINodeId parentItem{};
    UILogicalRect invocationAnchorRect{};
    UIMenuMetrics committedMetrics{};
    MenuLayoutScratch layoutScratch{};
    bool hasInvocationAnchorRect = false;
    bool open = false;
};

struct MenuItemState final {
    UINodeId node{};
    UIMenuItemConfig config{};
    UINodeId menu{};
    UINodeId submenu{};
    bool checked = false;
};

// Independent bounded sparse state owned by one UIContext. UIContext validates
// tree topology and coordinates dirty publication before mutating this storage.
class UIMenuStateStorage final {
  public:
    UIMenuStateStorage(usize menuCapacity, usize menuItemCapacity,
                       std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeMenuCount() const noexcept;
    [[nodiscard]] usize activeMenuItemCount() const noexcept;
    [[nodiscard]] usize availableMenuCount() const noexcept;
    [[nodiscard]] usize availableMenuItemCount() const noexcept;
    [[nodiscard]] bool containsMenu(UINodeId menu) const noexcept;
    [[nodiscard]] bool containsMenuItem(UINodeId item) const noexcept;
    [[nodiscard]] MenuState* tryMenu(UINodeId menu) noexcept;
    [[nodiscard]] const MenuState* tryMenu(UINodeId menu) const noexcept;
    [[nodiscard]] MenuItemState* tryItem(UINodeId item) noexcept;
    [[nodiscard]] const MenuItemState* tryItem(UINodeId item) const noexcept;
    [[nodiscard]] MenuLayoutScratch& layoutScratchByIndex(u32 nodeIndex) noexcept;

    [[nodiscard]] bool initializeMenu(
        UINodeId menu, const UIMenuConfig& config) noexcept;
    [[nodiscard]] bool initializeMenuItem(
        UINodeId item, UINodeId menu,
        const UIMenuItemConfig& config) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    [[nodiscard]] bool hasRelationship(UINodeId menu, UINodeId anchor) const noexcept;
    [[nodiscard]] UINodeId menuForAnchor(UINodeId anchor) const noexcept;
    [[nodiscard]] UINodeId anchorForMenu(UINodeId menu) const noexcept;
    void linkAnchorValidated(UINodeId menu, UINodeId anchor) noexcept;
    [[nodiscard]] UINodeId unlinkMenu(UINodeId menu) noexcept;
    [[nodiscard]] UINodeId unlinkAnchor(UINodeId anchor) noexcept;

    [[nodiscard]] bool hasSubmenuRelationship(UINodeId item,
                                               UINodeId submenu) const noexcept;
    [[nodiscard]] UINodeId submenuForItem(UINodeId item) const noexcept;
    [[nodiscard]] UINodeId parentItemForMenu(UINodeId menu) const noexcept;
    [[nodiscard]] UINodeId parentMenu(UINodeId menu) const noexcept;
    void linkSubmenuValidated(UINodeId item, UINodeId submenu) noexcept;
    [[nodiscard]] UINodeId unlinkSubmenuItem(UINodeId item) noexcept;
    [[nodiscard]] UINodeId unlinkSubmenuMenu(UINodeId submenu) noexcept;

    [[nodiscard]] UINodeId menuForItem(UINodeId item) const noexcept;
    [[nodiscard]] bool hasInvocationAnchorRect(UINodeId menu) const noexcept;
    [[nodiscard]] UILogicalRect invocationAnchorRect(UINodeId menu) const noexcept;
    void setInvocationAnchorRectValidated(UINodeId menu,
                                          UILogicalRect rect) noexcept;
    void clearInvocationAnchorRect(UINodeId menu) noexcept;
    [[nodiscard]] bool isOpen(UINodeId menu) const noexcept;
    [[nodiscard]] bool isInActiveChain(UINodeId menu) const noexcept;
    [[nodiscard]] UINodeId rootMenu() const noexcept;
    [[nodiscard]] UINodeId activeMenu() const noexcept;
    [[nodiscard]] UINodeId activeChildMenu(UINodeId menu) const noexcept;
    [[nodiscard]] UINodeId openRootValidated(UINodeId menu) noexcept;
    [[nodiscard]] UINodeId openSubmenuValidated(UINodeId menu) noexcept;
    [[nodiscard]] bool close(UINodeId menu) noexcept;

    [[nodiscard]] bool itemChecked(UINodeId item) const noexcept;
    [[nodiscard]] bool hasCheckedRadioPeer(UINodeId item) const noexcept;
    [[nodiscard]] bool setItemChecked(UINodeId item, bool checked) noexcept;

    void publishMetrics(u32 menuIndex) noexcept;
    [[nodiscard]] UIMenuMetrics committedMetrics(UINodeId menu) const noexcept;

  private:
    UIBoundedNodeStateTable<MenuState> menus_;
    UIBoundedNodeStateTable<MenuItemState> items_;
    UINodeId activeMenu_{};
};

} // namespace Tina::UI::Detail
