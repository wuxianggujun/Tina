#include <gtest/gtest.h>

#include "detail/UITextEditModel.hpp"

#include <limits>

namespace Tina::Tests {
namespace {

TEST(UITextEditModelTests, DetectsEitherSingleLineDelimiter)
{
    EXPECT_FALSE(UI::Detail::containsLineBreak("one line"));
    EXPECT_TRUE(UI::Detail::containsLineBreak("first\nsecond"));
    EXPECT_TRUE(UI::Detail::containsLineBreak("first\rsecond"));
}

TEST(UITextEditModelTests, MapsUtf8ScalarOffsetsToByteOffsetsAndClamps)
{
    constexpr std::string_view Text = "A\xE4\xB8\xAD" "B";

    EXPECT_EQ(UI::Detail::utf8ByteOffsetForCodepoint(Text, 0), 0U);
    EXPECT_EQ(UI::Detail::utf8ByteOffsetForCodepoint(Text, 1), 1U);
    EXPECT_EQ(UI::Detail::utf8ByteOffsetForCodepoint(Text, 2), 4U);
    EXPECT_EQ(UI::Detail::utf8ByteOffsetForCodepoint(Text, 3), 5U);
    EXPECT_EQ(UI::Detail::utf8ByteOffsetForCodepoint(Text, 100), 5U);
}

TEST(UITextEditModelTests, HorizontalMoveCollapsesOrExtendsSelection)
{
    const UI::UITextSelection selected{
        .anchorCodepoint = 5,
        .caretCodepoint = 2,
    };

    const auto left = UI::Detail::planTextEditCommand(
        selected, 8, UI::UITextEditCommand::MoveLeft, false);
    const auto right = UI::Detail::planTextEditCommand(
        selected, 8, UI::UITextEditCommand::MoveRight, false);
    const auto extended = UI::Detail::planTextEditCommand(
        selected, 8, UI::UITextEditCommand::MoveRight, true);

    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2, .caretCodepoint = 2}));
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(right->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 5, .caretCodepoint = 5}));
    ASSERT_TRUE(extended.has_value());
    EXPECT_EQ(extended->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 5, .caretCodepoint = 3}));
}

TEST(UITextEditModelTests, HomeEndAndSelectAllPreserveAnchorOnlyWhenExtending)
{
    const UI::UITextSelection current{
        .anchorCodepoint = 3,
        .caretCodepoint = 4,
    };

    const auto home = UI::Detail::planTextEditCommand(
        current, 8, UI::UITextEditCommand::MoveHome, true);
    const auto end = UI::Detail::planTextEditCommand(
        current, 8, UI::UITextEditCommand::MoveEnd, false);
    const auto all = UI::Detail::planTextEditCommand(
        current, 8, UI::UITextEditCommand::SelectAll, false);

    ASSERT_TRUE(home.has_value());
    EXPECT_EQ(home->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 3, .caretCodepoint = 0}));
    ASSERT_TRUE(end.has_value());
    EXPECT_EQ(end->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 8, .caretCodepoint = 8}));
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 0, .caretCodepoint = 8}));
}

TEST(UITextEditModelTests, BackspaceAndDeletePlanScalarRanges)
{
    const auto backspace = UI::Detail::planTextEditCommand(
        UI::UITextSelection{.anchorCodepoint = 4, .caretCodepoint = 4}, 8,
        UI::UITextEditCommand::Backspace, false);
    const auto deleteSelection = UI::Detail::planTextEditCommand(
        UI::UITextSelection{.anchorCodepoint = 6, .caretCodepoint = 2}, 8,
        UI::UITextEditCommand::Delete, false);

    ASSERT_TRUE(backspace.has_value());
    EXPECT_TRUE(backspace->deletesText);
    EXPECT_EQ(backspace->deleteBeginCodepoint, 3U);
    EXPECT_EQ(backspace->deleteEndCodepoint, 4U);
    ASSERT_TRUE(deleteSelection.has_value());
    EXPECT_TRUE(deleteSelection->deletesText);
    EXPECT_EQ(deleteSelection->deleteBeginCodepoint, 2U);
    EXPECT_EQ(deleteSelection->deleteEndCodepoint, 6U);
}

TEST(UITextEditModelTests, RightwardCommandsDoNotWrapAtMaximumCount)
{
    constexpr u32 Maximum = (std::numeric_limits<u32>::max)();
    const UI::UITextSelection atEnd{
        .anchorCodepoint = Maximum,
        .caretCodepoint = Maximum,
    };

    const auto move = UI::Detail::planTextEditCommand(
        atEnd, Maximum, UI::UITextEditCommand::MoveRight, false);
    const auto erase = UI::Detail::planTextEditCommand(
        atEnd, Maximum, UI::UITextEditCommand::Delete, false);

    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(move->nextSelection, atEnd);
    ASSERT_TRUE(erase.has_value());
    EXPECT_EQ(erase->deleteBeginCodepoint, Maximum);
    EXPECT_EQ(erase->deleteEndCodepoint, Maximum);
}

TEST(UITextEditModelTests, RejectsUnknownCommandValue)
{
    EXPECT_FALSE(UI::Detail::planTextEditCommand(
                     {}, 0, static_cast<UI::UITextEditCommand>(0xFF), false)
                     .has_value());
}

} // namespace
} // namespace Tina::Tests
