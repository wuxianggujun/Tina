#include <gtest/gtest.h>

#include "detail/UICommandPressLatch.hpp"

#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UITreeView.hpp>

namespace Tina::Tests {
namespace {

using DropdownLatch = UI::Detail::UICommandPressLatch<
    UI::UIDropdownCommand, UI::UIDropdownCommand::ExitNext>;
using ListViewLatch = UI::Detail::UICommandPressLatch<
    UI::UIListViewCommand, UI::UIListViewCommand::Activate>;
using TreeViewLatch = UI::Detail::UICommandPressLatch<
    UI::UITreeViewCommand, UI::UITreeViewCommand::Activate>;

TEST(UICommandPressLatchTests, AcceptsOnlyZeroBasedCommandRange)
{
    EXPECT_TRUE(DropdownLatch::accepts(UI::UIDropdownCommand::PreviousItem));
    EXPECT_TRUE(DropdownLatch::accepts(UI::UIDropdownCommand::ExitNext));
    EXPECT_FALSE(DropdownLatch::accepts(
        static_cast<UI::UIDropdownCommand>(255)));

    EXPECT_TRUE(ListViewLatch::accepts(UI::UIListViewCommand::Activate));
    EXPECT_TRUE(TreeViewLatch::accepts(UI::UITreeViewCommand::Activate));
}

TEST(UICommandPressLatchTests, ConsumedPressStaysLatchedUntilRelease)
{
    DropdownLatch latch;
    EXPECT_FALSE(latch.isLatched(UI::UIDropdownCommand::Dismiss));
    EXPECT_FALSE(latch.release(UI::UIDropdownCommand::Dismiss));

    latch.latch(UI::UIDropdownCommand::Dismiss);
    EXPECT_TRUE(latch.isLatched(UI::UIDropdownCommand::Dismiss));
    EXPECT_TRUE(latch.release(UI::UIDropdownCommand::Dismiss));
    EXPECT_FALSE(latch.isLatched(UI::UIDropdownCommand::Dismiss));
    EXPECT_FALSE(latch.release(UI::UIDropdownCommand::Dismiss));
}

TEST(UICommandPressLatchTests, CommandsAreIndependentAndClearDropsEveryLatch)
{
    TreeViewLatch latch;
    latch.latch(UI::UITreeViewCommand::PreviousItem);
    latch.latch(UI::UITreeViewCommand::ToggleExpanded);

    EXPECT_TRUE(latch.release(UI::UITreeViewCommand::PreviousItem));
    EXPECT_TRUE(latch.isLatched(UI::UITreeViewCommand::ToggleExpanded));

    latch.clear();
    EXPECT_FALSE(latch.isLatched(UI::UITreeViewCommand::ToggleExpanded));
}

} // namespace
} // namespace Tina::Tests
