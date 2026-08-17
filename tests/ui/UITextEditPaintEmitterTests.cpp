#include <gtest/gtest.h>

#include "detail/UITextEditPaintEmitter.hpp"

#include <tina/ui/UIErrors.hpp>

#include <array>
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

class BorrowInvalidatingRasterizer final : public UI::IUITextRasterizer {
  public:
    [[nodiscard]] Core::Result<UI::UIFontFaceId> openFace(
        std::span<const std::byte>, i32) override
    {
        return Face;
    }

    [[nodiscard]] Core::Status closeFace(UI::UIFontFaceId) noexcept override
    {
        return Core::success();
    }

    [[nodiscard]] Core::Result<UI::UITextMetrics> measure(
        UI::UIFontFaceId, std::string_view utf8, UI::UITextStyle style) override
    {
        return UI::UITextMetrics{
            .measuredSize = {
                .width = static_cast<float>(utf8.size()) * 5.0F,
                .height = utf8.empty() ? 0.0F : style.logicalSize * style.lineHeightScale,
            },
            .codepointCount = static_cast<u32>(utf8.size()),
            .lineCount = utf8.empty() ? 0U : 1U,
        };
    }

    [[nodiscard]] Core::Result<UI::UITextRasterBatch> raster(
        UI::UIFontFaceId, std::string_view utf8, UI::UITextStyle style) override
    {
        ++rasterCallCount;
        usize glyphCount = 0;
        for (const char character : utf8)
        {
            if (character == '\n')
            {
                continue;
            }
            if (glyphCount >= glyphs.size())
            {
                return Core::failure(UI::UIErrorCode::CapacityExceeded,
                                     "Borrow-invalidating rasterizer capacity exhausted");
            }
            glyphs[glyphCount++] = UI::UITextGlyphRaster{
                .codepoint = static_cast<u32>(static_cast<unsigned char>(character)),
                .advance = character == 'B' ? 7.0F : character == '^' ? 0.0F : 3.0F,
            };
        }
        return UI::UITextRasterBatch{
            .metrics = {
                .measuredSize = {
                    .width = 0.0F,
                    .height = utf8.empty() ? 0.0F : style.logicalSize * style.lineHeightScale,
                },
                .codepointCount = static_cast<u32>(utf8.size()),
                .lineCount = utf8.empty() ? 0U : 1U,
            },
            .baselineFromLineTop = style.logicalSize * style.lineHeightScale,
            .glyphs = std::span<const UI::UITextGlyphRaster>(glyphs.data(), glyphCount),
            .coverage = {},
        };
    }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {.faceCapacity = 1, .maxGlyphsPerRaster = static_cast<u32>(glyphs.size()),
                .coverageByteCapacity = 1};
    }

    static constexpr UI::UIFontFaceId Face{.index = 0, .generation = 1};
    std::array<UI::UITextGlyphRaster, 8> glyphs{};
    u32 rasterCallCount = 0;
};

TEST(UITextEditPaintEmitterTests, VisualRowsEmitPerRowSelectionAndCaret)
{
    const std::array lines{
        UI::Detail::UITextEditVisualLine{.beginCodepoint = 0, .endCodepoint = 1,
                                         .beginGlyphIndex = 0, .width = 5.0F, .top = 0.0F},
        UI::Detail::UITextEditVisualLine{.beginCodepoint = 2, .endCodepoint = 3,
                                         .beginGlyphIndex = 1, .width = 5.0F, .top = 15.0F},
    };
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .committedText = "A\nB",
        .selection = {.anchorCodepoint = 0, .caretCodepoint = 3},
        .style = testStyle(),
        .textColor = textColor(),
        .multilineEnabled = true,
        .visualLines = lines,
        .visualLayout = {.lineCount = 2, .lineHeight = 15.0F, .contentHeight = 30.0F},
    };
    EXPECT_EQ(UI::Detail::UITextEditPaintEmitter::countEntries(state), 5U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(5);
    u32 nextPaintOrdinal = 0;
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 5U);
    EXPECT_EQ(output[0].solidFill, state.selectionColor);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 27.0F);
    EXPECT_FLOAT_EQ(output[2].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[3].worldRect.y, 27.0F);
    EXPECT_FLOAT_EQ(output[4].worldRect.x, 15.0F);
    EXPECT_FLOAT_EQ(output[4].worldRect.y, 27.0F);
    ASSERT_TRUE(caret.has_value());
    EXPECT_EQ(caret->worldRect, output[4].worldRect);
    EXPECT_EQ(nextPaintOrdinal, 5U);
}

TEST(UITextEditPaintEmitterTests, SoftWrapCaretAffinitySelectsTheSharedBoundaryRow)
{
    const std::array lines{
        UI::Detail::UITextEditVisualLine{
            .beginCodepoint = 0, .endCodepoint = 1,
            .beginGlyphIndex = 0, .width = 5.0F, .top = 0.0F},
        UI::Detail::UITextEditVisualLine{
            .beginCodepoint = 1, .endCodepoint = 2,
            .beginGlyphIndex = 1, .width = 5.0F, .top = 15.0F},
    };
    UI::Detail::UITextEditPaintState state{
        .focused = true,
        .committedText = "AB",
        .selection = {.anchorCodepoint = 1, .caretCodepoint = 1},
        .caretAffinity = UI::Detail::UITextEditCaretAffinity::Upstream,
        .style = testStyle(),
        .textColor = textColor(),
        .multilineEnabled = true,
        .wrapMode = UI::UITextEditWrapMode::SoftWrap,
        .visualLines = lines,
        .visualLayout = {.lineCount = 2, .lineHeight = 15.0F, .contentHeight = 30.0F},
    };

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(UI::Detail::UITextEditPaintEmitter::countEntries(state));
    u32 nextPaintOrdinal = 0;
    auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);
    ASSERT_TRUE(caret.has_value());
    EXPECT_FLOAT_EQ(caret->worldRect.y, 12.0F);

    state.caretAffinity = UI::Detail::UITextEditCaretAffinity::Downstream;
    output.clear();
    nextPaintOrdinal = 0;
    caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);
    ASSERT_TRUE(caret.has_value());
    EXPECT_FLOAT_EQ(caret->worldRect.y, 27.0F);
}

TEST(UITextEditPaintEmitterTests, MultilineLfPaintKeepsRowsClippedAndCountsDrawableGlyphs)
{
    const UI::Detail::UITextEditPaintState state{
        .committedText = "A\nB",
        .style = testStyle(),
        .textColor = textColor(),
    };
    EXPECT_EQ(UI::Detail::UITextEditPaintEmitter::countEntries(state), 2U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 0;
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 27.0F);
    EXPECT_EQ(output[0].effectiveClip, testLayout().contentPlacement.contentBox);
    EXPECT_FALSE(caret.has_value());
    EXPECT_EQ(nextPaintOrdinal, 2U);
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
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, layout, nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 3U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 12.0F);
    EXPECT_FLOAT_EQ(output[2].worldRect.x, 20.0F);
    EXPECT_EQ(output[0].effectiveClip, layout.contentPlacement.contentBox);
    EXPECT_EQ(output[0].paintOrdinal, 4U);
    EXPECT_EQ(output[2].paintOrdinal, 6U);
    EXPECT_FALSE(caret.has_value());
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
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 5U);
    EXPECT_EQ(output[0].solidFill, textColor());
    EXPECT_EQ(output[1].kind, UI::UICommittedPaintKind::SolidQuad);
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
    ASSERT_TRUE(caret.has_value());
    EXPECT_EQ(caret->worldRect, output[4].worldRect);
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
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

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
    ASSERT_TRUE(caret.has_value());
    EXPECT_EQ(caret->worldRect, output[4].worldRect);
    EXPECT_EQ(nextPaintOrdinal, 13U);
}

TEST(UITextEditPaintEmitterTests, VisualRowsConsumeOneBorrowedRasterBatchAndSkipLfGlyphSlots)
{
    BorrowInvalidatingRasterizer rasterizer;
    const std::array lines{
        UI::Detail::UITextEditVisualLine{
            .beginCodepoint = 0, .endCodepoint = 1, .beginGlyphIndex = 0,
            .hardBreakCodepoint = 1, .width = 3.0F, .top = 0.0F},
        UI::Detail::UITextEditVisualLine{
            .beginCodepoint = 2, .endCodepoint = 3, .beginGlyphIndex = 1,
            .width = 7.0F, .top = 15.0F},
    };
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .committedText = "A\nB",
        .selection = {.anchorCodepoint = 3, .caretCodepoint = 3},
        .style = testStyle(),
        .textColor = textColor(),
        .rasterSource = {.rasterizer = &rasterizer,
                         .face = BorrowInvalidatingRasterizer::Face},
        .multilineEnabled = true,
        .visualLines = lines,
        .visualLayout = {.lineCount = 2, .lineHeight = 15.0F, .contentHeight = 30.0F},
    };

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(UI::Detail::UITextEditPaintEmitter::countEntries(state));
    u32 nextPaintOrdinal = 0;
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

    EXPECT_EQ(rasterizer.rasterCallCount, 1U);
    ASSERT_EQ(output.size(), 3U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 27.0F);
    ASSERT_TRUE(caret.has_value());
    EXPECT_FLOAT_EQ(caret->worldRect.x, 17.0F);
    EXPECT_FLOAT_EQ(caret->worldRect.y, 27.0F);
}

TEST(UITextEditPaintEmitterTests, ZeroAdvanceGlyphDoesNotMoveFollowingGlyphOrCaret)
{
    BorrowInvalidatingRasterizer rasterizer;
    const std::array lines{
        UI::Detail::UITextEditVisualLine{
            .beginCodepoint = 0, .endCodepoint = 3, .beginGlyphIndex = 0,
            .width = 10.0F, .top = 0.0F},
    };
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .committedText = "A^B",
        .selection = {.anchorCodepoint = 3, .caretCodepoint = 3},
        .style = testStyle(),
        .textColor = textColor(),
        .rasterSource = {.rasterizer = &rasterizer,
                         .face = BorrowInvalidatingRasterizer::Face},
        .multilineEnabled = true,
        .visualLines = lines,
        .visualLayout = {.lineCount = 1, .lineHeight = 15.0F, .contentHeight = 15.0F},
    };

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(UI::Detail::UITextEditPaintEmitter::countEntries(state));
    u32 nextPaintOrdinal = 0;
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, testLayout(), nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 4U);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 13.0F);
    EXPECT_FLOAT_EQ(output[2].worldRect.x, 13.0F);
    ASSERT_TRUE(caret.has_value());
    EXPECT_FLOAT_EQ(caret->worldRect.x, 20.0F);
}

TEST(UITextEditPaintEmitterTests, MultilinePreeditUsesSoftWrapAndVerticalScrollForCaret)
{
    const UI::Detail::UITextEditPaintState state{
        .focused = true,
        .preeditActive = true,
        .committedText = "ABCD",
        .selection = {.anchorCodepoint = 2, .caretCodepoint = 2},
        .preeditText = "xy",
        .preeditCursorCodepoint = 1,
        .style = testStyle(),
        .textColor = textColor(),
        .multilineEnabled = true,
        .wrapMode = UI::UITextEditWrapMode::SoftWrap,
        .scrollY = 15.0F,
    };
    UI::UICommittedLayoutEntry layout = testLayout();
    layout.contentPlacement.contentBox.width = 12.0F;

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(UI::Detail::UITextEditPaintEmitter::countEntries(state));
    u32 nextPaintOrdinal = 0;
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(
        output, layout, nextPaintOrdinal, state);

    ASSERT_EQ(output.size(), 7U);
    ASSERT_TRUE(caret.has_value());
    EXPECT_FLOAT_EQ(caret->worldRect.x, 15.0F);
    EXPECT_FLOAT_EQ(caret->worldRect.y, 12.0F);
    EXPECT_EQ(caret->effectiveClip,
              (UI::UILogicalRect{.x = 5.0F, .y = 6.0F, .width = 12.0F, .height = 30.0F}));
    EXPECT_EQ(output.back().worldRect, caret->worldRect);
}

} // namespace
} // namespace Tina::Tests
