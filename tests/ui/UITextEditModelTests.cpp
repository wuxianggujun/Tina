#include <gtest/gtest.h>

#include "detail/UIGraphemeBreak.hpp"
#include "detail/UITextEditModel.hpp"

#include <array>
#include <limits>
#include <optional>
#include <string_view>

namespace Tina::Tests {
namespace {

TEST(UITextEditModelTests, VisualCommandsUseRowsPreferredXAndLineBoundaries)
{
    constexpr std::string_view Text = "AB\nX\nAB";
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 18.0F},
        UI::UITextGlyphRaster{.advance = 2.0F},
        UI::UITextGlyphRaster{.advance = 5.0F},
        UI::UITextGlyphRaster{.advance = 4.0F},
        UI::UITextGlyphRaster{.advance = 16.0F},
    };
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        Text, 100.0F, 30.0F, 10.0F, 10.0F, UI::UITextEditWrapMode::NoWrap,
        glyphs, lines, layout));
    ASSERT_EQ(layout.lineCount, 3U);
    const auto visualLines = std::span(lines).first(layout.lineCount);

    const auto firstDown = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 1, .caretCodepoint = 1},
        UI::UITextEditCommand::MoveDown, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream, std::nullopt, 10.0F, glyphs);
    ASSERT_TRUE(firstDown.has_value());
    EXPECT_EQ(firstDown->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 4, .caretCodepoint = 4}));
    ASSERT_TRUE(firstDown->updatedPreferredX.has_value());
    EXPECT_FLOAT_EQ(*firstDown->updatedPreferredX, 18.0F);

    const auto secondDown = UI::Detail::planTextEditVisualCommand(
        Text, firstDown->nextSelection, UI::UITextEditCommand::MoveDown, false,
        visualLines, firstDown->nextCaretAffinity, firstDown->updatedPreferredX,
        10.0F, glyphs);
    ASSERT_TRUE(secondDown.has_value());
    EXPECT_EQ(secondDown->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 7, .caretCodepoint = 7}));
    EXPECT_EQ(secondDown->updatedPreferredX, firstDown->updatedPreferredX);

    const auto firstUp = UI::Detail::planTextEditVisualCommand(
        Text, secondDown->nextSelection, UI::UITextEditCommand::MoveUp, false,
        visualLines, secondDown->nextCaretAffinity, secondDown->updatedPreferredX,
        10.0F, glyphs);
    ASSERT_TRUE(firstUp.has_value());
    EXPECT_EQ(firstUp->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 4, .caretCodepoint = 4}));
    const auto secondUp = UI::Detail::planTextEditVisualCommand(
        Text, firstUp->nextSelection, UI::UITextEditCommand::MoveUp, false,
        visualLines, firstUp->nextCaretAffinity, firstUp->updatedPreferredX,
        10.0F, glyphs);
    ASSERT_TRUE(secondUp.has_value());
    EXPECT_EQ(secondUp->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 1}));

    const auto home = UI::Detail::planTextEditVisualCommand(
        Text, firstDown->nextSelection, UI::UITextEditCommand::MoveHome, false,
        visualLines, firstDown->nextCaretAffinity, firstDown->updatedPreferredX,
        10.0F, glyphs);
    ASSERT_TRUE(home.has_value());
    EXPECT_EQ(home->nextSelection, (UI::UITextSelection{.anchorCodepoint = 3, .caretCodepoint = 3}));
    EXPECT_FALSE(home->updatedPreferredX.has_value());

    EXPECT_FALSE(UI::Detail::planTextEditVisualCommand(
                     Text, {.anchorCodepoint = 1, .caretCodepoint = 1},
                     UI::UITextEditCommand::MoveDown, false, visualLines,
                     UI::Detail::UITextEditCaretAffinity::Downstream,
                     (std::numeric_limits<float>::quiet_NaN)(), 10.0F, glyphs)
                     .has_value());
    const float maximum = (std::numeric_limits<float>::max)();
    const std::array overflowingGlyphs{
        UI::UITextGlyphRaster{.advance = maximum},
        UI::UITextGlyphRaster{.advance = maximum},
        UI::UITextGlyphRaster{.advance = 5.0F},
        UI::UITextGlyphRaster{.advance = 4.0F},
        UI::UITextGlyphRaster{.advance = 16.0F},
    };
    EXPECT_FALSE(UI::Detail::planTextEditVisualCommand(
                     Text, {.anchorCodepoint = 2, .caretCodepoint = 2},
                     UI::UITextEditCommand::MoveDown, false, visualLines,
                     UI::Detail::UITextEditCaretAffinity::Downstream,
                     std::nullopt, 10.0F, overflowingGlyphs)
                     .has_value());
}
TEST(UITextEditModelTests, BuildsHardBreakAndSoftWrappedVisualLinesWithoutAllocation)
{
    std::array<UI::Detail::UITextEditVisualLine, 8> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        "A\n\nBC", 15.0F, 20.0F, 10.0F, 10.0F, UI::UITextEditWrapMode::SoftWrap,
        {}, lines, layout));
    ASSERT_EQ(layout.lineCount, 4U);
    EXPECT_EQ(lines[0].beginCodepoint, 0U);
    EXPECT_EQ(lines[0].endCodepoint, 1U);
    EXPECT_EQ(lines[0].beginGlyphIndex, 0U);
    EXPECT_EQ(lines[1].beginCodepoint, 2U);
    EXPECT_EQ(lines[1].endCodepoint, 2U);
    EXPECT_EQ(lines[1].beginGlyphIndex, 1U);
    EXPECT_EQ(lines[2].beginCodepoint, 3U);
    EXPECT_EQ(lines[2].endCodepoint, 4U);
    EXPECT_EQ(lines[2].beginGlyphIndex, 1U);
    EXPECT_EQ(lines[3].beginCodepoint, 4U);
    EXPECT_EQ(lines[3].endCodepoint, 5U);
    EXPECT_EQ(lines[3].beginGlyphIndex, 2U);
    EXPECT_FLOAT_EQ(layout.contentHeight, 40.0F);
    EXPECT_FLOAT_EQ(layout.maximumScrollY, 20.0F);
}

TEST(UITextEditModelTests, VisualHitAndNavigationUseDrawableGlyphOffsetsAfterLf)
{
    constexpr std::string_view Text = "A\nBC";
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 3.0F},
        UI::UITextGlyphRaster{.advance = 20.0F},
        UI::UITextGlyphRaster{.advance = 4.0F},
    };
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        Text, 100.0F, 20.0F, 10.0F, 8.0F, UI::UITextEditWrapMode::NoWrap,
        glyphs, lines, layout));
    ASSERT_EQ(layout.lineCount, 2U);
    EXPECT_EQ(lines[1].beginGlyphIndex, 1U);
    EXPECT_FLOAT_EQ(lines[1].width, 24.0F);

    EXPECT_EQ(UI::Detail::textEditHitFromVisualPosition(
                  Text, 9.9F, 15.0F, 0.0F, layout, lines, 8.0F, glyphs).codepoint,
              2U);
    EXPECT_EQ(UI::Detail::textEditHitFromVisualPosition(
                  Text, 10.0F, 15.0F, 0.0F, layout, lines, 8.0F, glyphs).codepoint,
              3U);
    const auto down = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 1, .caretCodepoint = 1},
        UI::UITextEditCommand::MoveDown, false, std::span(lines).first(layout.lineCount),
        UI::Detail::UITextEditCaretAffinity::Downstream,
        std::optional<float>{10.0F}, 8.0F, glyphs);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 3, .caretCodepoint = 3}));
}

TEST(UITextEditModelTests, VisualHitTestingUsesRowAndGlyphMidpoints)
{
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 20.0F},
        UI::UITextGlyphRaster{.advance = 10.0F},
    };
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        "ABC", 20.0F, 10.0F, 10.0F, 10.0F, UI::UITextEditWrapMode::SoftWrap,
        glyphs, lines, layout));
    ASSERT_EQ(layout.lineCount, 3U);
    const auto firstRowHit = UI::Detail::textEditHitFromVisualPosition(
        "ABC", 4.0F, 5.0F, 0.0F, layout, lines, 10.0F, glyphs);
    EXPECT_EQ(firstRowHit.codepoint, 0U);
    EXPECT_EQ(firstRowHit.affinity, UI::Detail::UITextEditCaretAffinity::Downstream);

    const auto secondRowEndHit = UI::Detail::textEditHitFromVisualPosition(
        "ABC", 11.0F, 15.0F, 0.0F, layout, lines, 10.0F, glyphs);
    EXPECT_EQ(secondRowEndHit.codepoint, 2U);
    EXPECT_EQ(secondRowEndHit.affinity, UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto finalRowHit = UI::Detail::textEditHitFromVisualPosition(
        "ABC", 100.0F, 100.0F, 0.0F, layout, lines, 10.0F, glyphs);
    EXPECT_EQ(finalRowHit.codepoint, 3U);
    EXPECT_EQ(finalRowHit.affinity, UI::Detail::UITextEditCaretAffinity::Downstream);
}

TEST(UITextEditModelTests, SoftWrapNavigationKeepsSharedBoundaryOnTheTargetRow)
{
    constexpr std::string_view Text = "ABC";
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 20.0F},
        UI::UITextGlyphRaster{.advance = 10.0F},
    };
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        Text, 20.0F, 30.0F, 10.0F, 10.0F, UI::UITextEditWrapMode::SoftWrap,
        glyphs, lines, layout));
    ASSERT_EQ(layout.lineCount, 3U);
    const auto visualLines = std::span(lines).first(layout.lineCount);

    const auto down = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 1U, .caretCodepoint = 1U},
        UI::UITextEditCommand::MoveDown, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Upstream,
        std::nullopt, 10.0F, glyphs);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(
        down->nextSelection,
        (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(down->nextCaretAffinity, UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto nextDown = UI::Detail::planTextEditVisualCommand(
        Text, down->nextSelection, UI::UITextEditCommand::MoveDown, false,
        visualLines, down->nextCaretAffinity, down->updatedPreferredX,
        10.0F, glyphs);
    ASSERT_TRUE(nextDown.has_value());
    EXPECT_EQ(
        nextDown->nextSelection,
        (UI::UITextSelection{.anchorCodepoint = 3U, .caretCodepoint = 3U}));
    EXPECT_EQ(nextDown->nextCaretAffinity, UI::Detail::UITextEditCaretAffinity::Downstream);

    const auto collapseDown = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 0U, .caretCodepoint = 2U},
        UI::UITextEditCommand::MoveDown, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Upstream, std::nullopt, 10.0F, glyphs);
    ASSERT_TRUE(collapseDown.has_value());
    EXPECT_EQ(collapseDown->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(collapseDown->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Downstream);

    const auto collapseUp = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 2U, .caretCodepoint = 3U},
        UI::UITextEditCommand::MoveUp, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream, std::nullopt, 10.0F, glyphs);
    ASSERT_TRUE(collapseUp.has_value());
    EXPECT_EQ(collapseUp->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(collapseUp->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto collapseEnd = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 0U, .caretCodepoint = 2U},
        UI::UITextEditCommand::MoveEnd, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream, std::nullopt, 10.0F, glyphs);
    ASSERT_TRUE(collapseEnd.has_value());
    EXPECT_EQ(collapseEnd->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(collapseEnd->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Upstream);
}

TEST(UITextEditModelTests, SoftWrapHorizontalNavigationVisitsBothBoundarySides)
{
    constexpr std::string_view Text = "ABCD";
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        Text, 20.0F, 20.0F, 10.0F, 10.0F,
        UI::UITextEditWrapMode::SoftWrap, {}, lines, layout));
    ASSERT_EQ(layout.lineCount, 2U);
    const auto visualLines = std::span(lines).first(layout.lineCount);

    const auto firstRight = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 1U, .caretCodepoint = 1U},
        UI::UITextEditCommand::MoveRight, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream,
        std::nullopt, 10.0F);
    ASSERT_TRUE(firstRight.has_value());
    EXPECT_EQ(firstRight->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(firstRight->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto secondRight = UI::Detail::planTextEditVisualCommand(
        Text, firstRight->nextSelection, UI::UITextEditCommand::MoveRight, false,
        visualLines, firstRight->nextCaretAffinity, std::nullopt, 10.0F);
    ASSERT_TRUE(secondRight.has_value());
    EXPECT_EQ(secondRight->nextSelection, firstRight->nextSelection);
    EXPECT_EQ(secondRight->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Downstream);

    const auto firstLeft = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 3U, .caretCodepoint = 3U},
        UI::UITextEditCommand::MoveLeft, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream,
        std::nullopt, 10.0F);
    ASSERT_TRUE(firstLeft.has_value());
    EXPECT_EQ(firstLeft->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(firstLeft->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Downstream);

    const auto secondLeft = UI::Detail::planTextEditVisualCommand(
        Text, firstLeft->nextSelection, UI::UITextEditCommand::MoveLeft, false,
        visualLines, firstLeft->nextCaretAffinity, std::nullopt, 10.0F);
    ASSERT_TRUE(secondLeft.has_value());
    EXPECT_EQ(secondLeft->nextSelection, firstLeft->nextSelection);
    EXPECT_EQ(secondLeft->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto collapseRight = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 0U, .caretCodepoint = 2U},
        UI::UITextEditCommand::MoveRight, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream,
        std::nullopt, 10.0F);
    ASSERT_TRUE(collapseRight.has_value());
    EXPECT_EQ(collapseRight->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(collapseRight->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Upstream);

    const auto collapseLeft = UI::Detail::planTextEditVisualCommand(
        Text, {.anchorCodepoint = 2U, .caretCodepoint = 4U},
        UI::UITextEditCommand::MoveLeft, false, visualLines,
        UI::Detail::UITextEditCaretAffinity::Downstream,
        std::nullopt, 10.0F);
    ASSERT_TRUE(collapseLeft.has_value());
    EXPECT_EQ(collapseLeft->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    EXPECT_EQ(collapseLeft->nextCaretAffinity,
              UI::Detail::UITextEditCaretAffinity::Downstream);
}

TEST(UITextEditModelTests, RejectsVisualLineCapacityOverflow)
{
    std::array<UI::Detail::UITextEditVisualLine, 1> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    EXPECT_FALSE(UI::Detail::buildTextEditVisualLayout(
        "A\nB", 100.0F, 10.0F, 10.0F, 10.0F, UI::UITextEditWrapMode::NoWrap,
        {}, lines, layout));
}
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

TEST(UITextEditModelTests, SegmentsCommonExtendedGraphemeClusters)
{
    constexpr std::string_view Text =
        "A"
        "e\xCC\x81"
        "\xE4\xB8\xAD"
        "\xF0\x9F\x91\xA9" "\xE2\x80\x8D" "\xF0\x9F\x92\xBB"
        "\xF0\x9F\x87\xA8" "\xF0\x9F\x87\xB3"
        "\xF0\x9F\x91\x8D" "\xF0\x9F\x8F\xBD";
    constexpr std::array<u32, 6> ExpectedEnds{1U, 3U, 4U, 7U, 9U, 11U};

    usize byteOffset = 0;
    u32 codepointOffset = 0;
    UI::Detail::UIGraphemeCluster cluster{};
    u32 expectedBegin = 0;
    for (const u32 expectedEnd : ExpectedEnds)
    {
        ASSERT_TRUE(UI::Detail::nextGraphemeCluster(
            Text, byteOffset, codepointOffset, cluster));
        EXPECT_EQ(cluster.beginCodepoint, expectedBegin);
        EXPECT_EQ(cluster.endCodepoint, expectedEnd);
        expectedBegin = expectedEnd;
    }
    EXPECT_FALSE(UI::Detail::nextGraphemeCluster(
        Text, byteOffset, codepointOffset, cluster));
    EXPECT_EQ(byteOffset, Text.size());
    EXPECT_EQ(codepointOffset, 11U);

    EXPECT_TRUE(UI::Detail::isGraphemeBoundary(Text, 0U));
    EXPECT_TRUE(UI::Detail::isGraphemeBoundary(Text, 11U));
    EXPECT_FALSE(UI::Detail::isGraphemeBoundary(Text, 2U));
    EXPECT_FALSE(UI::Detail::isGraphemeBoundary(Text, 5U));
    EXPECT_FALSE(UI::Detail::isGraphemeBoundary(Text, 8U));
    EXPECT_FALSE(UI::Detail::isGraphemeBoundary(Text, 10U));
    EXPECT_EQ(UI::Detail::previousGraphemeBoundary(Text, 7U), 4U);
    EXPECT_EQ(UI::Detail::nextGraphemeBoundary(Text, 4U), 7U);
    EXPECT_EQ(UI::Detail::graphemeBoundaryAtOrAfter(Text, 5U), 7U);
    EXPECT_EQ(UI::Detail::nearestGraphemeBoundary(Text, 2U), 3U);
}

TEST(UITextEditModelTests, SoftWrapAndHorizontalHitNeverSplitACluster)
{
    constexpr std::string_view Text = "A" "e\xCC\x81" "B";
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 0.0F},
        UI::UITextGlyphRaster{.advance = 10.0F},
    };
    std::array<UI::Detail::UITextEditVisualLine, 4> lines{};
    UI::Detail::UITextEditVisualLayout layout{};
    ASSERT_TRUE(UI::Detail::buildTextEditVisualLayout(
        Text, 15.0F, 30.0F, 10.0F, 10.0F,
        UI::UITextEditWrapMode::SoftWrap, glyphs, lines, layout));
    ASSERT_EQ(layout.lineCount, 3U);
    EXPECT_EQ(lines[0].beginCodepoint, 0U);
    EXPECT_EQ(lines[0].endCodepoint, 1U);
    EXPECT_EQ(lines[1].beginCodepoint, 1U);
    EXPECT_EQ(lines[1].endCodepoint, 3U);
    EXPECT_EQ(lines[2].beginCodepoint, 3U);
    EXPECT_EQ(lines[2].endCodepoint, 4U);

    constexpr std::string_view ClusterThenAscii = "e\xCC\x81" "B";
    const std::span<const UI::UITextGlyphRaster> clusterGlyphs =
        std::span<const UI::UITextGlyphRaster>(glyphs).subspan(1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  ClusterThenAscii, 4.9F, 10.0F, clusterGlyphs),
              0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  ClusterThenAscii, 5.0F, 10.0F, clusterGlyphs),
              2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  ClusterThenAscii, 14.9F, 10.0F, clusterGlyphs),
              2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  ClusterThenAscii, 15.0F, 10.0F, clusterGlyphs),
              3U);
}

TEST(UITextEditModelTests, NavigationAndDeletionUseWholeGraphemeClusters)
{
    constexpr std::string_view Text =
        "A" "e\xCC\x81" "\xE4\xB8\xAD";
    const auto left = UI::Detail::planTextEditCommand(
        Text, {.anchorCodepoint = 3U, .caretCodepoint = 3U},
        UI::UITextEditCommand::MoveLeft, false);
    const auto right = UI::Detail::planTextEditCommand(
        Text, {.anchorCodepoint = 1U, .caretCodepoint = 1U},
        UI::UITextEditCommand::MoveRight, false);
    const auto backspace = UI::Detail::planTextEditCommand(
        Text, {.anchorCodepoint = 3U, .caretCodepoint = 3U},
        UI::UITextEditCommand::Backspace, false);
    const auto erase = UI::Detail::planTextEditCommand(
        Text, {.anchorCodepoint = 1U, .caretCodepoint = 1U},
        UI::UITextEditCommand::Delete, false);
    const auto splitSelection = UI::Detail::planTextEditCommand(
        Text, {.anchorCodepoint = 2U, .caretCodepoint = 2U},
        UI::UITextEditCommand::MoveRight, false);

    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 1U, .caretCodepoint = 1U}));
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(right->nextSelection,
              (UI::UITextSelection{.anchorCodepoint = 3U, .caretCodepoint = 3U}));
    ASSERT_TRUE(backspace.has_value());
    EXPECT_TRUE(backspace->deletesText);
    EXPECT_EQ(backspace->deleteBeginCodepoint, 1U);
    EXPECT_EQ(backspace->deleteEndCodepoint, 3U);
    ASSERT_TRUE(erase.has_value());
    EXPECT_TRUE(erase->deletesText);
    EXPECT_EQ(erase->deleteBeginCodepoint, 1U);
    EXPECT_EQ(erase->deleteEndCodepoint, 3U);
    EXPECT_FALSE(splitSelection.has_value());
}

TEST(UITextEditModelTests, PointerCaretFallbackUsesGraphemeMidpointsAndClamps)
{
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", -1.0F, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("", 5.0F, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 4.9F, 10.0F), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 5.0F, 10.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 14.9F, 10.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 15.0F, 10.0F), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 100.0F, 10.0F), 3U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 0.5F, 0.0F), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", (std::numeric_limits<float>::quiet_NaN)(), 10.0F),
              0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", (std::numeric_limits<float>::infinity)(), 10.0F),
              3U);
}

TEST(UITextEditModelTests, PointerCaretUsesGlyphAdvancesWithFallbackAndOverflowGuards)
{
    const std::array glyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = 20.0F},
        UI::UITextGlyphRaster{.advance = 5.0F},
    };

    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 4.9F, 8.0F, glyphs), 0U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 5.0F, 8.0F, glyphs), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 19.9F, 8.0F, glyphs), 1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 20.0F, 8.0F, glyphs), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 32.4F, 8.0F, glyphs), 2U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition("ABC", 32.5F, 8.0F, glyphs), 3U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", (std::numeric_limits<float>::infinity)(), 8.0F, glyphs),
              3U);

    const std::array invalidAdvanceGlyphs{
        UI::UITextGlyphRaster{.advance = 10.0F},
        UI::UITextGlyphRaster{.advance = -1.0F},
        UI::UITextGlyphRaster{.advance = 5.0F},
    };
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", 13.9F, 8.0F, invalidAdvanceGlyphs),
              1U);
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", 14.0F, 8.0F, invalidAdvanceGlyphs),
              2U);

    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "ABC", 5.0F, 10.0F,
                  std::span<const UI::UITextGlyphRaster>(glyphs).first(2)),
              1U);

    const float maximum = (std::numeric_limits<float>::max)();
    const std::array overflowingGlyphs{
        UI::UITextGlyphRaster{.advance = maximum},
        UI::UITextGlyphRaster{.advance = maximum},
    };
    EXPECT_EQ(UI::Detail::textEditCodepointFromHorizontalPosition(
                  "AB", maximum, 1.0F, overflowingGlyphs),
              1U);
}

TEST(UITextEditModelTests, HorizontalMoveCollapsesOrExtendsSelection)
{
    const UI::UITextSelection selected{
        .anchorCodepoint = 5,
        .caretCodepoint = 2,
    };

    const auto left = UI::Detail::planTextEditCommand(
        "ABCDEFGH", selected, UI::UITextEditCommand::MoveLeft, false);
    const auto right = UI::Detail::planTextEditCommand(
        "ABCDEFGH", selected, UI::UITextEditCommand::MoveRight, false);
    const auto extended = UI::Detail::planTextEditCommand(
        "ABCDEFGH", selected, UI::UITextEditCommand::MoveRight, true);

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
        "ABCDEFGH", current, UI::UITextEditCommand::MoveHome, true);
    const auto end = UI::Detail::planTextEditCommand(
        "ABCDEFGH", current, UI::UITextEditCommand::MoveEnd, false);
    const auto all = UI::Detail::planTextEditCommand(
        "ABCDEFGH", current, UI::UITextEditCommand::SelectAll, false);

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

TEST(UITextEditModelTests, SingleLineVerticalCommandsAreRecognizedNoOps)
{
    constexpr UI::UITextSelection Selection{
        .anchorCodepoint = 1,
        .caretCodepoint = 1,
    };

    const auto up = UI::Detail::planTextEditCommand(
        "ABC", Selection, UI::UITextEditCommand::MoveUp, false);
    const auto down = UI::Detail::planTextEditCommand(
        "ABC", Selection, UI::UITextEditCommand::MoveDown, true);

    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(up->nextSelection, Selection);
    EXPECT_FALSE(up->deletesText);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->nextSelection, Selection);
    EXPECT_FALSE(down->deletesText);
}

TEST(UITextEditModelTests, RightwardCommandsClampAtTheActualTextEnd)
{
    const UI::UITextSelection atEnd{
        .anchorCodepoint = 1,
        .caretCodepoint = 1,
    };

    const auto move = UI::Detail::planTextEditCommand(
        "A", atEnd, UI::UITextEditCommand::MoveRight, false);
    const auto erase = UI::Detail::planTextEditCommand(
        "A", atEnd, UI::UITextEditCommand::Delete, false);

    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(move->nextSelection, atEnd);
    ASSERT_TRUE(erase.has_value());
    EXPECT_EQ(erase->deleteBeginCodepoint, 1U);
    EXPECT_EQ(erase->deleteEndCodepoint, 1U);
}

TEST(UITextEditModelTests, RejectsUnknownCommandValue)
{
    EXPECT_FALSE(UI::Detail::planTextEditCommand(
                     "", {}, static_cast<UI::UITextEditCommand>(0xFF), false)
                     .has_value());
}

} // namespace
} // namespace Tina::Tests
