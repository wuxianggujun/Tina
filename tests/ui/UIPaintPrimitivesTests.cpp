#include <gtest/gtest.h>

#include "detail/UIPaintPrimitives.hpp"

#include <array>
#include <string_view>

namespace Tina::Tests {
namespace {

TEST(UIPaintPrimitivesTests, CountMatchesEmittedShadowFillAndBorders)
{
    const UI::UIBoxPaint paint{
        .solidFill = UI::UISolidFill{.color = UI::rgb(0x204060)},
        .borderLight = UI::rgb(0xFFFFFF),
        .borderDark = UI::rgb(0x101010),
        .borderWidth = 2.0F,
        .shadow = UI::rgb(0x000000, 128),
        .shadowOffsetX = 3.0F,
        .shadowOffsetY = 4.0F,
    };
    const UI::UILogicalRect worldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 100.0F,
        .height = 40.0F,
    };
    const UI::UILogicalRect clip{
        .x = 0.0F,
        .y = 0.0F,
        .width = 200.0F,
        .height = 100.0F,
    };
    const auto fill = UI::premultiply(paint.solidFill->color);

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    entries.reserve(6);
    u32 ordinal = 7;
    UI::Detail::appendBoxChromePaints(entries, UI::UINodeId{}, worldRect,
                                      clip, ordinal, paint, fill);

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(paint, worldRect, true),
              6U);
    ASSERT_EQ(entries.size(), 6U);
    EXPECT_EQ(ordinal, 13U);
    for (usize index = 0; index < entries.size(); ++index)
    {
        EXPECT_EQ(entries[index].paintOrdinal, 7U + index);
        EXPECT_EQ(entries[index].effectiveClip, clip);
    }
    EXPECT_EQ(entries[0].worldRect,
              (UI::UILogicalRect{.x = 13.0F, .y = 24.0F, .width = 100.0F, .height = 40.0F}));
    EXPECT_EQ(entries[1].solidFill, fill);
}

TEST(UIPaintPrimitivesTests, InvisibleChromeEmitsNothing)
{
    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 3;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, UI::UILogicalRect{}, UI::UILogicalRect{},
        ordinal, UI::UIBoxPaint{}, UI::UIPremultipliedRgba8Color{});

    EXPECT_TRUE(entries.empty());
    EXPECT_EQ(ordinal, 3U);
    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  UI::UIBoxPaint{}, UI::UILogicalRect{}, false),
              0U);
}

TEST(UIPaintPrimitivesTests, RoundedChromeEmitsShadowOuterBorderAndInsetFill)
{
    const UI::UIBoxPaint paint{
        .solidFill = UI::UISolidFill{.color = UI::rgb(0x204060)},
        .borderLight = UI::rgb(0xFFFFFF),
        .borderDark = UI::rgb(0x101010),
        .borderWidth = 2.0F,
        .shadow = UI::rgb(0x000000, 128),
        .shadowOffsetX = 3.0F,
        .shadowOffsetY = 4.0F,
        .cornerRadius = 8.0F,
    };
    const UI::UILogicalRect worldRect{.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 40.0F};
    const UI::UILogicalRect clip{.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 100.0F};
    const auto fill = UI::premultiply(paint.solidFill->color);

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 5;
    UI::Detail::appendBoxChromePaints(entries, UI::UINodeId{}, worldRect, clip, ordinal, paint, fill);

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(paint, worldRect, true), 3U);
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(ordinal, 8U);
    EXPECT_FLOAT_EQ(entries[0].cornerRadius, 8.0F);
    EXPECT_EQ(entries[1].worldRect, worldRect);
    EXPECT_EQ(entries[1].solidFill, UI::premultiply(paint.borderDark));
    EXPECT_FLOAT_EQ(entries[1].cornerRadius, 8.0F);
    EXPECT_EQ(entries[2].worldRect,
              (UI::UILogicalRect{.x = 12.0F, .y = 22.0F, .width = 96.0F, .height = 36.0F}));
    EXPECT_EQ(entries[2].solidFill, fill);
    EXPECT_FLOAT_EQ(entries[2].cornerRadius, 6.0F);
}

TEST(UIPaintPrimitivesTests, DrawableTextCountSkipsNewlinesAndStopsAtTruncation)
{
    EXPECT_EQ(UI::Detail::countDrawableTextCodepoints("A\n\xE4\xB8\xAD"), 2U);
    const std::array truncated{static_cast<char>(0xE4), static_cast<char>(0xB8)};
    EXPECT_EQ(UI::Detail::countDrawableTextCodepoints(
                  std::string_view(truncated.data(), truncated.size())),
              0U);
}

TEST(UIPaintPrimitivesTests, OpacityScalesEveryPremultipliedChannel)
{
    const UI::UIPremultipliedRgba8Color color{
        .red = 80,
        .green = 40,
        .blue = 20,
        .alpha = 160,
    };
    const auto result = UI::Detail::applyOpacity(color, 128);

    EXPECT_EQ(result.red, 40);
    EXPECT_EQ(result.green, 20);
    EXPECT_EQ(result.blue, 10);
    EXPECT_EQ(result.alpha, 80);
}

} // namespace
} // namespace Tina::Tests
