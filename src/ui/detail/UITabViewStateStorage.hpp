#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UITabView.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

struct TabViewLayoutScratch final {
    UITabViewMetrics metrics{};
};

struct TabViewState final {
    UINodeId node{};
    UITabViewConfig config{};
    UINodeId firstTab{};
    UINodeId lastTab{};
    UINodeId activeTab{};
    u32 itemCount = 0;
    UITabViewMetrics committedMetrics{};
    TabViewLayoutScratch layoutScratch{};
};

struct TabState final {
    UINodeId node{};
    UITabConfig config{};
    UITabPaint paint{};
    UINodeId tabView{};
    UINodeId panel{};
    UINodeId previousTab{};
    UINodeId nextTab{};
    u32 ordinal = 0;
};

// Bounded sparse relationship storage. UIContext validates tree topology and
// coordinates dirty publication; this module owns link lifetime.
class UITabViewStateStorage final {
  public:
    UITabViewStateStorage(
        usize tabViewCapacity, usize tabCapacity,
        std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize availableTabViewCount() const noexcept;
    [[nodiscard]] usize availableTabCount() const noexcept;
    [[nodiscard]] bool containsTabView(UINodeId tabView) const noexcept;
    [[nodiscard]] bool containsTab(UINodeId tab) const noexcept;
    [[nodiscard]] TabViewState* tryTabView(UINodeId tabView) noexcept;
    [[nodiscard]] const TabViewState* tryTabView(UINodeId tabView) const noexcept;
    [[nodiscard]] TabState* tryTab(UINodeId tab) noexcept;
    [[nodiscard]] const TabState* tryTab(UINodeId tab) const noexcept;
    [[nodiscard]] UITabPaint& tabPaintByIndex(u32 nodeIndex) noexcept;
    [[nodiscard]] const UITabPaint& tabPaintByIndex(u32 nodeIndex) const noexcept;
    [[nodiscard]] TabViewLayoutScratch& layoutScratchByIndex(u32 nodeIndex) noexcept;

    [[nodiscard]] bool initializeTabView(
        UINodeId tabView, const UITabViewConfig& config) noexcept;
    [[nodiscard]] bool initializeTab(
        UINodeId tab, const UITabConfig& config) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    void linkValidated(UINodeId tabView, std::span<const UITabViewItem> items,
                       u32 activeIndex) noexcept;
    void unlinkTabView(UINodeId tabView) noexcept;
    [[nodiscard]] bool relationshipValid(UINodeId tabView) const noexcept;
    [[nodiscard]] UINodeId tabViewForTab(UINodeId tab) const noexcept;
    [[nodiscard]] UINodeId tabViewForPanel(UINodeId panel) const noexcept;
    [[nodiscard]] UINodeId tabForPanel(UINodeId panel) const noexcept;
    [[nodiscard]] UITabViewItem itemAt(UINodeId tabView, u32 index) const noexcept;
    [[nodiscard]] u32 itemCount(UINodeId tabView) const noexcept;
    [[nodiscard]] UINodeId activeTab(UINodeId tabView) const noexcept;
    [[nodiscard]] UINodeId activePanel(UINodeId tabView) const noexcept;
    [[nodiscard]] bool setActiveTab(UINodeId tabView, UINodeId tab) noexcept;

    void publishMetrics(u32 tabViewIndex) noexcept;
    [[nodiscard]] UITabViewMetrics committedMetrics(UINodeId tabView) const noexcept;

  private:
    UIBoundedNodeStateTable<TabViewState> tabViews_;
    UIBoundedNodeStateTable<TabState> tabs_;
};

} // namespace Tina::UI::Detail
