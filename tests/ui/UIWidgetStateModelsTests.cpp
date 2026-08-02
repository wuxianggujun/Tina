#include <gtest/gtest.h>

#include "detail/UIWidgetStateModels.hpp"

namespace Tina::Tests {
namespace {

TEST(UIWidgetStateModelsTests, TextAndProgressControlsStartFromStableDefaults)
{
    const UI::Detail::WidgetTextState text{};
    const UI::Detail::ProgressBarState progressBar{};
    const UI::Detail::RadioButtonState radioButton{};

    EXPECT_EQ(text.allocation.offset, 0U);
    EXPECT_EQ(text.allocation.capacity, 0U);
    EXPECT_EQ(text.length, 0U);
    EXPECT_FALSE(text.hasContent);
    EXPECT_FLOAT_EQ(progressBar.minValue, 0.0F);
    EXPECT_FLOAT_EQ(progressBar.maxValue, 1.0F);
    EXPECT_FLOAT_EQ(progressBar.value, 0.0F);
    EXPECT_FALSE(radioButton.selected);
}

TEST(UIWidgetStateModelsTests, ContainersStartClosedUnboundAndAtOrigin)
{
    const UI::Detail::DropdownState dropdown{};
    const UI::Detail::PopupState popup{};
    const UI::Detail::ListViewState listView{};
    const UI::Detail::TreeViewState treeView{};

    EXPECT_FALSE(dropdown.popup.hasValue());
    EXPECT_FALSE(popup.open);
    EXPECT_FALSE(listView.dataSource.hasValue());
    EXPECT_FALSE(listView.selection.hasValue());
    EXPECT_EQ(listView.materializedItemCapacity, 0U);
    EXPECT_FALSE(treeView.dataSource.hasValue());
    EXPECT_FALSE(treeView.selection.hasValue());
    EXPECT_EQ(treeView.materializedItemCapacity, 0U);
}

TEST(UIWidgetStateModelsTests, VirtualRowsStartUnboundButEnabled)
{
    const UI::Detail::ListViewItemState listItem{};
    const UI::Detail::TreeViewItemState treeItem{};

    EXPECT_EQ(listItem.key, UI::InvalidUIListViewItemKey);
    EXPECT_FALSE(listItem.bound);
    EXPECT_TRUE(listItem.enabled);
    EXPECT_EQ(listItem.committedKey, UI::InvalidUIListViewItemKey);
    EXPECT_FALSE(listItem.committedBound);
    EXPECT_TRUE(listItem.committedEnabled);
    EXPECT_EQ(treeItem.key, UI::InvalidUITreeViewItemKey);
    EXPECT_FALSE(treeItem.bound);
    EXPECT_TRUE(treeItem.enabled);
    EXPECT_FALSE(treeItem.expandable);
    EXPECT_FALSE(treeItem.expanded);
    EXPECT_EQ(treeItem.committedKey, UI::InvalidUITreeViewItemKey);
    EXPECT_FALSE(treeItem.committedBound);
    EXPECT_TRUE(treeItem.committedEnabled);
}

} // namespace
} // namespace Tina::Tests
