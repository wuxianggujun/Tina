#include <gtest/gtest.h>

#include "detail/UIPropertyNormalization.hpp"

#include <tina/ui/UIErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

TEST(UIPropertyNormalizationTests, BoxPaintDropsInvisibleChrome)
{
    UI::UIBoxPaint paint{
        .solidFill = UI::UISolidFill{.color = UI::rgba8(10, 20, 30, 0)},
        .borderLight = UI::rgb(0xFFFFFF),
        .borderDark = UI::rgb(0x111111),
        .borderWidth = -4.0F,
        .shadow = UI::rgb(0x000000, 0),
        .shadowOffsetX = 3.0F,
        .shadowOffsetY = 5.0F,
        .cornerRadius = (std::numeric_limits<float>::quiet_NaN)(),
    };

    const UI::UIBoxPaint normalized =
        UI::Detail::normalizeBoxPaint(paint);
    EXPECT_FALSE(normalized.solidFill.has_value());
    EXPECT_EQ(normalized.borderWidth, 0.0F);
    EXPECT_EQ(normalized.borderLight, UI::UIStraightSrgba8Color{});
    EXPECT_EQ(normalized.borderDark, UI::UIStraightSrgba8Color{});
    EXPECT_EQ(normalized.shadow, UI::UIStraightSrgba8Color{});
    EXPECT_EQ(normalized.shadowOffsetX, 0.0F);
    EXPECT_EQ(normalized.shadowOffsetY, 0.0F);
    EXPECT_EQ(normalized.cornerRadius, 0.0F);
}

TEST(UIPropertyNormalizationTests, ContextCapacitiesDeriveFromNodeCapacity)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 32,
        .rootCapacity = 4,
        .textByteCapacity = 0,
        .applyDefaultProductChrome = false,
    };

    auto normalized = UI::Detail::normalizeUIContextCapacityConfig(config);

    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(normalized->nodeCapacity, 32U);
    EXPECT_EQ(normalized->rootCapacity, 4U);
    EXPECT_EQ(normalized->dirtyQueueCapacity, 32U);
    EXPECT_EQ(normalized->layoutSnapshotCapacity, 32U);
    EXPECT_EQ(normalized->hitSnapshotCapacity, 32U);
    EXPECT_EQ(normalized->paintSnapshotCapacity, 32U);
    EXPECT_EQ(normalized->routePathCapacity, 32U);
    EXPECT_EQ(normalized->routedPointerListenerCapacity, 32U);
    EXPECT_EQ(normalized->buttonActionCapacity, 32U);
    EXPECT_EQ(normalized->textByteCapacity,
              UI::UIContextCapacityConfig::DefaultTextByteCapacity);
    EXPECT_EQ(normalized->styleClassCapacity,
              UI::UIContextCapacityConfig::DefaultStyleClassCapacity);
    EXPECT_EQ(normalized->styleRuleCapacity,
              UI::UIContextCapacityConfig::DefaultStyleRuleCapacity);
    EXPECT_EQ(normalized->styleBucketCapacity,
              UI::UIContextCapacityConfig::DefaultStyleBucketCapacity);
    EXPECT_EQ(normalized->styleRulesPerBucketCapacity,
              UI::UIContextCapacityConfig::DefaultStyleRulesPerBucketCapacity);
    EXPECT_EQ(normalized->nodeStyleClassLinkCapacity, 128U);
    EXPECT_FALSE(normalized->applyDefaultProductChrome);
}

TEST(UIPropertyNormalizationTests, ContextCapacitiesPreserveOverridesAndRejectInvalidInput)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 32,
        .rootCapacity = 4,
        .dirtyQueueCapacity = 8,
        .layoutSnapshotCapacity = 9,
        .hitSnapshotCapacity = 10,
        .paintSnapshotCapacity = 11,
        .routePathCapacity = 12,
        .routedPointerListenerCapacity = 64,
        .buttonActionCapacity = 13,
        .textByteCapacity = 1024,
        .styleClassCapacity = 12,
        .styleRuleCapacity = 24,
        .styleBucketCapacity = 16,
        .styleRulesPerBucketCapacity = 6,
        .nodeStyleClassLinkCapacity = 20,
    };

    auto normalized = UI::Detail::normalizeUIContextCapacityConfig(config);

    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(normalized->dirtyQueueCapacity, 8U);
    EXPECT_EQ(normalized->layoutSnapshotCapacity, 9U);
    EXPECT_EQ(normalized->hitSnapshotCapacity, 10U);
    EXPECT_EQ(normalized->paintSnapshotCapacity, 11U);
    EXPECT_EQ(normalized->routePathCapacity, 12U);
    EXPECT_EQ(normalized->routedPointerListenerCapacity, 64U);
    EXPECT_EQ(normalized->buttonActionCapacity, 13U);
    EXPECT_EQ(normalized->textByteCapacity, 1024U);
    EXPECT_EQ(normalized->styleClassCapacity, 12U);
    EXPECT_EQ(normalized->styleRuleCapacity, 24U);
    EXPECT_EQ(normalized->styleBucketCapacity, 16U);
    EXPECT_EQ(normalized->styleRulesPerBucketCapacity, 6U);
    EXPECT_EQ(normalized->nodeStyleClassLinkCapacity, 20U);

    config.rootCapacity = 33;
    auto rejected = UI::Detail::normalizeUIContextCapacityConfig(config);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST(UIPropertyNormalizationTests, ContextRejectsInvalidStyleCapacities)
{
    UI::UIContextCapacityConfig config{.nodeCapacity = 8, .rootCapacity = 1};

    config.styleClassCapacity = 0;
    EXPECT_FALSE(UI::Detail::normalizeUIContextCapacityConfig(config).has_value());
    config.styleClassCapacity = 4;

    config.styleBucketCapacity = config.styleRuleCapacity + 1U;
    EXPECT_FALSE(UI::Detail::normalizeUIContextCapacityConfig(config).has_value());
    config.styleBucketCapacity = config.styleRuleCapacity;

    config.styleRulesPerBucketCapacity = config.styleRuleCapacity + 1U;
    EXPECT_FALSE(UI::Detail::normalizeUIContextCapacityConfig(config).has_value());
    config.styleRulesPerBucketCapacity = config.styleRuleCapacity;

    config.nodeStyleClassLinkCapacity = config.nodeCapacity * 4U + 1U;
    const auto rejected = UI::Detail::normalizeUIContextCapacityConfig(config);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST(UIPropertyNormalizationTests, LayoutCanonicalizesNegativeZeroAndRejectsInvalidEnum)
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(-0.0F);
    style.padding.left = -0.0F;
    auto normalized = UI::Detail::normalizeLayoutStyle(style);
    ASSERT_TRUE(normalized.has_value());
    EXPECT_FALSE(std::signbit(normalized->size.width.value));
    EXPECT_FALSE(std::signbit(normalized->padding.left));

    style.visibility = static_cast<UI::UIVisibility>(255);
    auto rejected = UI::Detail::normalizeLayoutStyle(style);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
}

TEST(UIPropertyNormalizationTests, ScrollOffsetRejectsNegativeAndCanonicalizesZero)
{
    auto normalized = UI::Detail::normalizeScrollOffset(
        UI::UIScrollOffset{.x = -0.0F, .y = 2.0F});
    ASSERT_TRUE(normalized.has_value());
    EXPECT_FALSE(std::signbit(normalized->x));
    EXPECT_EQ(normalized->y, 2.0F);

    auto rejected = UI::Detail::normalizeScrollOffset(
        UI::UIScrollOffset{.x = -1.0F, .y = 0.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
}

TEST(UIPropertyNormalizationTests, CollectionStylesRejectNonFiniteMetrics)
{
    UI::UIListViewStyle listStyle{};
    listStyle.rowHeight = (std::numeric_limits<float>::infinity)();
    auto listRejected = UI::Detail::normalizeListViewStyle(listStyle);
    ASSERT_FALSE(listRejected.has_value());
    EXPECT_EQ(listRejected.error().code,
              UI::UIErrorCode::InvalidControlValue);

    UI::UITreeViewStyle treeStyle{};
    treeStyle.indentation =
        (std::numeric_limits<float>::quiet_NaN)();
    auto treeRejected = UI::Detail::normalizeTreeViewStyle(treeStyle);
    ASSERT_FALSE(treeRejected.has_value());
    EXPECT_EQ(treeRejected.error().code,
              UI::UIErrorCode::InvalidControlValue);
}

TEST(UIPropertyNormalizationTests, ThemeRejectsNonFiniteMetrics)
{
    UI::UITheme theme = UI::makeDefaultProductTheme();
    theme.buttonTextSize =
        (std::numeric_limits<float>::quiet_NaN)();
    Core::Status status = UI::Detail::validateProductTheme(theme);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidTheme);
}

} // namespace
} // namespace Tina::Tests
