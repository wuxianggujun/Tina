#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UIStyleController.hpp>
#include <tina/ui/UITheme.hpp>

#include "detail/UIElementContractResolver.hpp"

#include <algorithm>
#include <memory>
#include <ranges>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId makeWindow()
{
    static auto windows = [] {
        auto result = WindowPool::Create(32);
        EXPECT_TRUE(result.has_value());
        return std::make_unique<WindowPool>(std::move(*result));
    }();
    auto window = windows->tryEmplace(1);
    EXPECT_TRUE(window.has_value());
    return window ? *window : Platform::WindowId{};
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window)
{
    auto result = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .routePathCapacity = 16,
            .buttonActionCapacity = 8,
        });
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle layout;
    layout.size.width = UI::UILayoutLength::Px(width);
    layout.size.height = UI::UILayoutLength::Px(height);
    return layout;
}

[[nodiscard]] const UI::UICommittedPaintEntry* firstSolidQuad(
    UI::UICommittedPaintView paint, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        paint.entries(), [node](const UI::UICommittedPaintEntry& entry) {
            return entry.node == node &&
                   entry.kind == UI::UICommittedPaintKind::SolidQuad;
        });
    return found == paint.entries().end() ? nullptr : &*found;
}

[[nodiscard]] const UI::UISemanticsEntry* semanticsFor(
    UI::UICommittedSemanticsView semantics, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        semantics.entries(), [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found == semantics.entries().end() ? nullptr : &*found;
}

TEST(UIVisualComponentsTests, RecipesPublishStrongTypedVisualProfiles)
{
    constexpr auto plain = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Plain});
    constexpr auto filled = UI::makeSurfaceElement();
    constexpr auto elevated = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Elevated});
    static_assert(plain.visual.styleRole == UI::UIStyleRoleId::None);
    static_assert(filled.visual.styleRole == UI::UIStyleRoleId::PanelSurface);
    static_assert(elevated.visual.styleRole == UI::UIStyleRoleId::PanelElevated);
    static_assert(filled.semantics.mode == UI::UISemanticsMode::Automatic);
    static_assert(filled.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);

    constexpr auto horizontal = UI::makeDividerElement();
    constexpr auto vertical = UI::makeDividerElement({
        .orientation = UI::UIDividerOrientation::Vertical,
        .tone = UI::UIDividerTone::Accent,
        .thickness = 2.0F,
    });
    static_assert(horizontal.layout.size.height == UI::UILayoutLength::Px(1.0F));
    static_assert(horizontal.visual.styleRole == UI::UIStyleRoleId::DividerSubtle);
    static_assert(vertical.layout.size.width == UI::UILayoutLength::Px(2.0F));
    static_assert(vertical.visual.styleRole == UI::UIStyleRoleId::DividerAccent);
    static_assert(vertical.semantics.mode == UI::UISemanticsMode::Exclude);
    static_assert(vertical.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);

    constexpr auto badge = UI::makeBadgeElement(
        "Error", {.tone = UI::UIBadgeTone::Danger});
    static_assert(badge.text == std::string_view{"Error"});
    static_assert(badge.visual.styleRole == UI::UIStyleRoleId::BadgeDanger);
    static_assert(badge.layout.padding.left == 8.0F);
    static_assert(badge.layout.padding.top == 3.0F);
    static_assert(badge.semantics.role == UI::UISemanticsRole::Label);
    static_assert(badge.semantics.useContentAsName);
    static_assert(badge.semantics.readOnly);
    static_assert(badge.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);

    EXPECT_EQ(*UI::Detail::resolveElementBuiltinKind(filled),
              UI::Detail::BuiltinElementKind::Panel);
    EXPECT_EQ(*UI::Detail::resolveElementBuiltinKind(horizontal),
              UI::Detail::BuiltinElementKind::Panel);
    EXPECT_EQ(*UI::Detail::resolveElementBuiltinKind(badge),
              UI::Detail::BuiltinElementKind::Label);
}

TEST(UIVisualComponentsTests, InvalidProfilesAndThicknessFailClosedAtCreate)
{
    auto context = createContext(makeWindow());
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(root);

    const auto invalidSurface = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeSurfaceElement({
                                .variant = static_cast<UI::UISurfaceVariant>(255),
                            }));
    EXPECT_FALSE(invalidSurface);
    EXPECT_EQ(invalidSurface.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    const auto invalidOrientation = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeDividerElement({
                                .orientation = static_cast<UI::UIDividerOrientation>(255),
                            }));
    EXPECT_FALSE(invalidOrientation);

    const auto invalidTone = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeBadgeElement(
                                "Badge",
                                {.tone = static_cast<UI::UIBadgeTone>(255)}));
    EXPECT_FALSE(invalidTone);

    const auto negativeThickness = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeDividerElement({
                                .thickness = -1.0F,
                            }));
    EXPECT_FALSE(negativeThickness);
    EXPECT_EQ(negativeThickness.error().code, UI::UIErrorCode::InvalidLayout);

    const auto explicitSizeNegativeThickness = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeDividerElement(
                                {.thickness = -1.0F}, fixedSize(80.0F, 4.0F)));
    EXPECT_FALSE(explicitSizeNegativeThickness);
    EXPECT_EQ(explicitSizeNegativeThickness.error().code, UI::UIErrorCode::InvalidLayout);
}

TEST(UIVisualComponentsTests, ThemeChromePaintAndSemanticsRemainUnified)
{
    auto context = createContext(makeWindow());
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(root);

    auto surface = context->authoring().rootBuilder().createElement(
        root->rootNodeId(), UI::makeSurfaceElement({}, fixedSize(120.0F, 30.0F)));
    auto divider = context->authoring().rootBuilder().createElement(
        root->rootNodeId(),
        UI::makeDividerElement(
            {.tone = UI::UIDividerTone::Accent, .thickness = 2.0F},
            fixedSize(120.0F, 2.0F)));
    auto badge = context->authoring().rootBuilder().createElement(
        root->rootNodeId(),
        UI::makeBadgeElement("Ready", {.tone = UI::UIBadgeTone::Accent},
                             fixedSize(72.0F, 24.0F)));
    ASSERT_TRUE(surface && divider && badge);

    auto updater = context->authoring().treeUpdater(*root);
    ASSERT_TRUE(updater);
    ASSERT_TRUE(updater->setLayoutStyle(root->rootNodeId(), fixedSize(160.0F, 100.0F)));
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 100.0F}));

    const UI::UITheme dark = UI::makeModernDesktopTheme();
    const UI::UICommittedPaintEntry* surfacePaint =
        firstSolidQuad(context->publication().committedPaint(), *surface);
    const UI::UICommittedPaintEntry* dividerPaint =
        firstSolidQuad(context->publication().committedPaint(), *divider);
    const UI::UICommittedPaintEntry* badgePaint =
        firstSolidQuad(context->publication().committedPaint(), *badge);
    ASSERT_NE(surfacePaint, nullptr);
    ASSERT_NE(dividerPaint, nullptr);
    ASSERT_NE(badgePaint, nullptr);
    EXPECT_EQ(surfacePaint->solidFill, UI::premultiply(dark.colors.surface));
    EXPECT_EQ(dividerPaint->solidFill,
              UI::premultiply(UI::scaleColorAlpha(dark.colors.primary, 190)));
    EXPECT_EQ(badgePaint->solidFill,
              UI::premultiply(UI::scaleColorAlpha(dark.colors.primaryContainer, 220)));
    EXPECT_EQ(updater->textStyle(*badge).value(),
              UI::makeBadgeChrome(dark, UI::UIBadgeTone::Accent).label);

    EXPECT_EQ(semanticsFor(context->publication().committedSemantics(), *surface), nullptr);
    EXPECT_EQ(semanticsFor(context->publication().committedSemantics(), *divider), nullptr);
    const UI::UISemanticsEntry* badgeSemantics =
        semanticsFor(context->publication().committedSemantics(), *badge);
    ASSERT_NE(badgeSemantics, nullptr);
    EXPECT_EQ(badgeSemantics->role, UI::UISemanticsRole::Label);
    EXPECT_EQ(badgeSemantics->name, "Ready");

    const auto dividerHit = std::ranges::find_if(
        context->publication().committedHit().entries(),
        [divider](const UI::UICommittedHitEntry& entry) {
            return entry.node == *divider;
        });
    ASSERT_NE(dividerHit, context->publication().committedHit().entries().end());
    EXPECT_EQ(dividerHit->policy, UI::UIPointerHitPolicy::Ignore);

    const UI::UITheme light = UI::makeModernDesktopTheme(UI::UIColorScheme::Light);
    ASSERT_TRUE(context->style().setProductTheme(light));
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 100.0F}));
    dividerPaint = firstSolidQuad(context->publication().committedPaint(), *divider);
    badgePaint = firstSolidQuad(context->publication().committedPaint(), *badge);
    ASSERT_NE(dividerPaint, nullptr);
    ASSERT_NE(badgePaint, nullptr);
    EXPECT_EQ(dividerPaint->solidFill,
              UI::premultiply(UI::scaleColorAlpha(light.colors.primary, 190)));
    EXPECT_EQ(badgePaint->solidFill,
              UI::premultiply(UI::scaleColorAlpha(light.colors.primaryContainer, 220)));
    EXPECT_EQ(updater->textStyle(*badge).value(),
              UI::makeBadgeChrome(light, UI::UIBadgeTone::Accent).label);
}

} // namespace
} // namespace Tina::Tests
