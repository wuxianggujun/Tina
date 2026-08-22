#include <gtest/gtest.h>

#include "detail/UIPaintPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
        .cornerRadii = {
            .topLeft = 8.0F,
            .topRight = 5.0F,
            .bottomRight = 2.0F,
            .bottomLeft = 0.0F,
        },
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
    EXPECT_EQ(entries[0].cornerRadii, paint.cornerRadii);
    EXPECT_EQ(entries[1].worldRect, worldRect);
    EXPECT_EQ(entries[1].solidFill, UI::premultiply(paint.borderDark));
    EXPECT_EQ(entries[1].cornerRadii, paint.cornerRadii);
    EXPECT_EQ(entries[2].worldRect,
              (UI::UILogicalRect{.x = 12.0F, .y = 22.0F, .width = 96.0F, .height = 36.0F}));
    EXPECT_EQ(entries[2].solidFill, fill);
    EXPECT_EQ(entries[2].cornerRadii,
              (UI::UILogicalCornerRadii{
                  .topLeft = 6.0F,
                  .topRight = 3.0F,
                  .bottomRight = 0.0F,
                  .bottomLeft = 0.0F,
              }));
}

TEST(UIPaintPrimitivesTests, EllipseEmitsOneCommittedEntryWithStroke)
{
    const UI::UIBoxPaint paint =
        UI::makeEllipseOutline(UI::rgb(0x336699), 3.0F);
    const UI::UILogicalRect worldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 40.0F,
        .height = 24.0F,
    };
    const UI::UILogicalRect clip{
        .x = 0.0F,
        .y = 0.0F,
        .width = 100.0F,
        .height = 80.0F,
    };
    const auto fill = UI::premultiply(paint.solidFill->color);

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 4;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, worldRect, clip, ordinal, paint, fill);

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  paint, worldRect, true),
              1U);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].kind, UI::UICommittedPaintKind::SolidEllipse);
    EXPECT_EQ(entries[0].worldRect, worldRect);
    EXPECT_EQ(entries[0].effectiveClip, clip);
    EXPECT_EQ(entries[0].solidFill, fill);
    EXPECT_FLOAT_EQ(entries[0].ellipseStrokeWidth, 3.0F);
    EXPECT_EQ(entries[0].paintOrdinal, 4U);
    EXPECT_EQ(ordinal, 5U);
}

TEST(UIPaintPrimitivesTests, EllipseWithOversizedStrokeEmitsNothing)
{
    const UI::UIBoxPaint paint =
        UI::makeEllipseOutline(UI::rgb(0x336699), 13.0F);
    const UI::UILogicalRect worldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 40.0F,
        .height = 24.0F,
    };
    const auto fill = UI::premultiply(paint.solidFill->color);

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 4;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, worldRect, worldRect, ordinal, paint, fill);

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  paint, worldRect, true),
              0U);
    EXPECT_TRUE(entries.empty());
    EXPECT_EQ(ordinal, 4U);
}

TEST(UIPaintPrimitivesTests, LineEmitsWorldGeometryAndConservativeEnvelope)
{
    const UI::UIBoxPaint paint = UI::makeSolidLine(
        UI::rgb(0xCC8844), {.x = 2.0F, .y = 3.0F},
        {.x = 12.0F, .y = 3.0F}, 4.0F);
    const UI::UILogicalRect worldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 40.0F,
        .height = 24.0F,
    };
    const UI::UILogicalRect clip{
        .x = 0.0F,
        .y = 0.0F,
        .width = 100.0F,
        .height = 80.0F,
    };

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 8;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, worldRect, clip, ordinal, paint,
        UI::premultiply(paint.solidFill->color));

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  paint, worldRect, true),
              1U);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].kind, UI::UICommittedPaintKind::SolidLine);
    EXPECT_EQ(entries[0].lineStart,
              (UI::UILogicalPoint{.x = 12.0F, .y = 23.0F}));
    EXPECT_EQ(entries[0].lineEnd,
              (UI::UILogicalPoint{.x = 22.0F, .y = 23.0F}));
    EXPECT_FLOAT_EQ(entries[0].lineThickness, 4.0F);
    EXPECT_EQ(entries[0].worldRect,
              (UI::UILogicalRect{
                  .x = 12.0F,
                  .y = 21.0F,
                  .width = 10.0F,
                  .height = 4.0F,
              }));
    EXPECT_EQ(ordinal, 9U);
}

TEST(UIPaintPrimitivesTests, LineEnvelopeRoundsOutwardAfterFloatConversion)
{
    const UI::UILineGeometry line{
        .start = {.x = 300.387054F, .y = 234.961182F},
        .end = {.x = 299.399994F, .y = 235.048965F},
        .thickness = 1.0F,
    };

    const auto geometry =
        UI::Detail::resolveCommittedLineGeometry(line, {});
    ASSERT_TRUE(geometry.has_value());

    const double deltaX = static_cast<double>(geometry->worldEnd.x) -
                          geometry->worldStart.x;
    const double deltaY = static_cast<double>(geometry->worldEnd.y) -
                          geometry->worldStart.y;
    const double length = std::hypot(deltaX, deltaY);
    const double halfThickness = static_cast<double>(line.thickness) * 0.5;
    const double extentX = std::abs(deltaY / length) * halfThickness;
    const double extentY = std::abs(deltaX / length) * halfThickness;
    const double requiredLeft =
        (std::min)(static_cast<double>(geometry->worldStart.x),
                   static_cast<double>(geometry->worldEnd.x)) - extentX;
    const double requiredTop =
        (std::min)(static_cast<double>(geometry->worldStart.y),
                   static_cast<double>(geometry->worldEnd.y)) - extentY;
    const double requiredRight =
        (std::max)(static_cast<double>(geometry->worldStart.x),
                   static_cast<double>(geometry->worldEnd.x)) + extentX;
    const double requiredBottom =
        (std::max)(static_cast<double>(geometry->worldStart.y),
                   static_cast<double>(geometry->worldEnd.y)) + extentY;
    const double envelopeRight =
        static_cast<double>(geometry->worldEnvelope.x) +
        geometry->worldEnvelope.width;
    const double envelopeBottom =
        static_cast<double>(geometry->worldEnvelope.y) +
        geometry->worldEnvelope.height;

    EXPECT_LE(static_cast<double>(geometry->worldEnvelope.x), requiredLeft);
    EXPECT_LE(static_cast<double>(geometry->worldEnvelope.y), requiredTop);
    EXPECT_GE(envelopeRight, requiredRight);
    EXPECT_GE(envelopeBottom, requiredBottom);
}

TEST(UIPaintPrimitivesTests, DegenerateLineEmitsNothingInsteadOfBoxRectangle)
{
    const UI::UIBoxPaint paint = UI::makeSolidLine(
        UI::rgb(0xFFFFFF), {.x = 4.0F, .y = 5.0F},
        {.x = 4.0F, .y = 5.0F}, 2.0F);
    const UI::UILogicalRect worldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 80.0F,
        .height = 60.0F,
    };

    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 2;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, worldRect, worldRect, ordinal, paint,
        UI::premultiply(paint.solidFill->color));

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  paint, worldRect, true),
              0U);
    EXPECT_TRUE(entries.empty());
    EXPECT_EQ(ordinal, 2U);
}

TEST(UIPaintPrimitivesTests, LineTranslationFailsClosedWhenFloatCoordinatesCollapseEndpoints)
{
    constexpr UI::UILogicalPoint WorldOrigin{.x = 16777216.0F, .y = 0.0F};
    const UI::UIBoxPaint paint = UI::makeSolidLine(
        UI::rgb(0xFFFFFF), {}, {.x = 1.0F, .y = 0.0F}, 2.0F);

    EXPECT_FALSE(UI::Detail::resolveCommittedLineGeometry(
                     paint.line, WorldOrigin)
                     .has_value());

    const UI::UILogicalRect worldRect{
        .x = WorldOrigin.x,
        .y = WorldOrigin.y,
        .width = 4.0F,
        .height = 4.0F,
    };
    std::pmr::vector<UI::UICommittedPaintEntry> entries;
    u32 ordinal = 7;
    UI::Detail::appendBoxChromePaints(
        entries, UI::UINodeId{}, worldRect, worldRect, ordinal, paint,
        UI::premultiply(paint.solidFill->color));

    EXPECT_EQ(UI::Detail::countBoxChromePaintEntries(
                  paint, worldRect, true),
              0U);
    EXPECT_TRUE(entries.empty());
    EXPECT_EQ(ordinal, 7U);
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
