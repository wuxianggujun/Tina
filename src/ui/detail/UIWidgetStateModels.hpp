#pragma once

#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITreeView.hpp>

#include "UITextStorage.hpp"

namespace Tina::UI::Detail {

struct WidgetTextState final {
    UITextStorage::Allocation allocation{};
    u32 length = 0;
    UITextStyle style{};
    UIContentAlignment alignment{};
    UITextMetrics metrics{};
    bool hasContent = false;
};

struct ProgressBarState final {
    float minValue = 0.0F;
    float maxValue = 1.0F;
    float value = 0.0F;
    UIProgressBarPaint paint{};
};

struct RadioButtonState final {
    UIRadioButtonPaint paint{};
    bool selected = false;
};

struct ScrollViewState final {
    UIScrollViewStyle style{};
    UIScrollViewPaint paint{};
    UIScrollOffset requestedOffset{};
    UIScrollViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
};

struct ScrollViewLayoutScratch final {
    UIScrollViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct DropdownState final {
    UINodeId popup{};
    UINodeId selectedItem{};
    UIDropdownPaint paint{};
};

struct PopupState final {
    UIPopupStyle style{};
    UIPopupMetrics committedMetrics{};
    bool open = false;
};

struct PopupLayoutScratch final {
    UIPopupMetrics metrics{};
};

struct ListViewState final {
    UIListViewStyle style{};
    UIListViewPaint paint{};
    UIListViewDataSource dataSource{};
    UIListViewSelection selection{};
    UIListViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
    float requestedScrollOffset = 0.0F;
    u32 materializedItemCapacity = 0;
};

struct ListViewLayoutScratch final {
    UIListViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct ListViewItemState final {
    UIListViewItemKey key = InvalidUIListViewItemKey;
    u64 logicalIndex = 0;
    bool bound = false;
    bool enabled = true;
    UIListViewItemKey committedKey = InvalidUIListViewItemKey;
    u64 committedLogicalIndex = 0;
    bool committedBound = false;
    bool committedEnabled = true;
};

struct TreeViewState final {
    UITreeViewStyle style{};
    UITreeViewPaint paint{};
    UITreeViewDataSource dataSource{};
    UITreeViewSelection selection{};
    UITreeViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
    float requestedScrollOffset = 0.0F;
    u32 materializedItemCapacity = 0;
};

struct TreeViewLayoutScratch final {
    UITreeViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct TreeViewItemState final {
    UITreeViewItemKey key = InvalidUITreeViewItemKey;
    u64 logicalIndex = 0;
    u32 level = 0;
    bool bound = false;
    bool enabled = true;
    bool expandable = false;
    bool expanded = false;
    UITreeViewItemKey committedKey = InvalidUITreeViewItemKey;
    u64 committedLogicalIndex = 0;
    u32 committedLevel = 0;
    bool committedBound = false;
    bool committedEnabled = true;
    bool committedExpandable = false;
    bool committedExpanded = false;
};

} // namespace Tina::UI::Detail
