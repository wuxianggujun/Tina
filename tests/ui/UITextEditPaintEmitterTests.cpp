#include <gtest/gtest.h>

#include "detail/UITextEditPaintEmitter.hpp"

#include <memory_resource>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::UITextStyle testStyle() noexcept
{
    return UI::UITextStyle{
        .logicalSize = 10.0F,
        .advanceScale = 0.5F,
        .lineHeightScale = 1.5F,
    };
}

[[nodiscard]] UI::UIPremultipliedRgba8Color textColor() noexcept
{
    return UI::premultiply(UI::rgb(0xDDE6ED));
}

[[nodiscard]] UI::UICommittedLayoutEntry testLayout() noexcept
{
    return UI::UICommittedLayoutEntry{
        .effectiveClip = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F},
        .contentPlacement =
            {
                .contentBox = {.x = 5.0F, .y = 6.0F, .width = 50.0F, .height = 30.0F},
                .origin = {.x = 10.0F, .y = 12.0F},
            },
    };
}

TEST(UITextEditPaintEmitterTests, CountsAndEmitsUnfocusedCommittedText)
{
    const UI::Detail::UITextEditPaintState state{
        .committedText = "ABC",
        .style = testStyle(),
        .textColor = textColor(),
    };
    EXPECT_EQ(UI::Detail::UITextEditPaintEmitter::countEntries(state), 3U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(3);
    u32 nextPaintOrdinal = 4;
    const UI::UICommittedLayoutEntry layout = testLayout();
    UI::Detail::UITextEditPaintEmitter::append(output, layout, nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 3U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[2].worldRect.x, 20.0F);
    EXPECT_EQ(output[0].effectiveClip, layout.contentPlacement.contentBox);
    EXPECT_EQ(output[0].paintOrdinal, 4U);
    EXPECT_EQ(output[2].paintOrdinal, 6U);
    EXPECT_EQ(nextPaintOrdinal, 7U);
}

TEST(UITextEditPaintEmitterTests, EmitsSelectionBeforeSelectedTextAndPlacesReverseCaretAtSelectionStart)
{
    const UI::UIPremultipliedRgba8Color selectionColor = UI::premultiply(UI::rgb(0x1266AA));
    const UI::UIPremultipliedRgba8Color caretColor = UI::premultiply(UI::rgb(0xF2C94C));
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .committedText = "ABC",
        .selection = {.anchorCodepoint = 2, .caretCodepoint = 1},
        .style = testStyle(),
        .textColor = textColor(),
        .selectionColor = selectionColor,
        .caretColor = caretColor,
    };
    EXPECT_EQ(UI::Detail::UITextEditPaintEmitter::countEntries(state), 5U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(5);
    u32 nextPaintOrdinal = 0;
    UI::Detail::UITextEditPaintEmitter::append(output, testLayout(), nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 5U);
    EXPECT_EQ(output[0].solidFill, textColor());
    EXPECT_FALSE(output[1].isGlyph);
    EXPECT_EQ(output[1].solidFill, selectionColor);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 15.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.width, 5.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.height, 15.0F);
    EXPECT_EQ(output[1].paintOrdinal, 1U);
    EXPECT_EQ(output[2].paintOrdinal, 2U);
    EXPECT_EQ(output[3].paintOrdinal, 3U);
    EXPECT_FLOAT_EQ(output[4].worldRect.x, 15.0F);
    EXPECT_FLOAT_EQ(output[4].worldRect.width, 2.0F);
    EXPECT_EQ(output[4].solidFill, caretColor);
    EXPECT_EQ(output[4].paintOrdinal, 4U);
    EXPECT_EQ(nextPaintOrdinal, 5U);
}

TEST(UITextEditPaintEmitterTests, ReplacesSelectionWithPreeditAndPlacesCaretAtPreeditCursor)
{
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .preeditActive = true,
        .committedText = "ABC",
        .selection = {.anchorCodepoint = 1, .caretCodepoint = 2},
        .preeditText = "xy",
        .preeditCursorCodepoint = 1,
        .style = testStyle(),
        .textColor = textColor(),
    };
    EXPECT_EQ(UI::Detail::UITextEditPaintEmitter::countEntries(state), 5U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(5);
    u32 nextPaintOrdinal = 8;
    UI::Detail::UITextEditPaintEmitter::append(output, testLayout(), nextPaintOrdinal, state);

    const UI::UIPremultipliedRgba8Color expectedPreedit = UI::premultiply(UI::UIStraightSrgba8Color{
        .red = 0,
        .green = 180,
        .blue = 255,
        .alpha = 255,
    });
    ASSERT_EQ(output.size(), 5U);
    EXPECT_EQ(output[0].solidFill, textColor());
    EXPECT_EQ(output[1].solidFill, expectedPreedit);
    EXPECT_EQ(output[2].solidFill, expectedPreedit);
    EXPECT_EQ(output[3].solidFill, textColor());
    EXPECT_FLOAT_EQ(output[4].worldRect.x, 20.0F);
    EXPECT_EQ(output[4].paintOrdinal, 12U);
    EXPECT_EQ(nextPaintOrdinal, 13U);
}

} // namespace
} // namespace Tina::Tests
