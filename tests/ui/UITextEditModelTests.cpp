#include <gtest/gtest.h>

#include "detail/UITextEditModel.hpp"

#include <array>
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

TEST(UITextEditModelTests, PointerCaretFallbackUsesScalarMidpointsAndClamps)
{
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(-1.0F, 3, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(5.0F, 0, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(4.9F, 3, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(5.0F, 3, 10.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(14.9F, 3, 10.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(15.0F, 3, 10.0F), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(100.0F, 3, 10.0F), 3U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(0.5F, 3, 0.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  (std::numeric_limits<float>::quiet_NaN)(), 3, 10.0F),
              0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  (std::numeric_limits<float>::infinity)(), 3, 10.0F),
              0U);
}

TEST(UITextEditModelTests, PointerCaretUsesGlyphAdvancesWithFallbackAndOverflowGuards)
{
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 20.0F},
        UI::UITextGlyphRaster{.advance = 5.0F},
    };

    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(4.9F, 3, 8.0F, glyphs), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(5.0F, 3, 8.0F, glyphs), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(19.9F, 3, 8.0F, glyphs), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(20.0F, 3, 8.0F, glyphs), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(32.4F, 3, 8.0F, glyphs), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(32.5F, 3, 8.0F, glyphs), 3U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  (std::numeric_limits<float>::infinity)(), 3, 8.0F, glyphs),
              3U);

    const std::array invalidAdvanceGlyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 0.0F},
        UI::UITextGlyphRaster{.advance = 5.0F},
    };
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  13.9F, 3, 8.0F, invalidAdvanceGlyphs),
              1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  14.0F, 3, 8.0F, invalidAdvanceGlyphs),
              2U);

    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  5.0F, 3, 10.0F,
                  std::span<const UI::UITextGlyphRaster>(glyphs).first(2)),
              1U);

    const float maximum = (std::numeric_limits<float>::max)();
    const std::array overflowingGlyphs{
        UI::UITextGlyphRaster{.advance = maximum},
        UI::UITextGlyphRaster{.advance = maximum},
    };
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  maximum, 2, 1.0F, overflowingGlyphs),
              1U);
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
