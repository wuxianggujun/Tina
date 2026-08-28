#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class FakeTooltipClock final : public Core::IMonotonicClock {
  public:
    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void advance(Core::Duration delta) noexcept
    {
        now_ += std::chrono::duration_cast<Core::MonotonicDuration>(delta);
    }

  private:
    Core::MonotonicTimePoint now_{};
};

[[nodiscard]] constexpr UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] constexpr UI::UILayoutStyle overlay(float x, float y, float width,
                                                  float height) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(x);
    style.overlay.offset.y = UI::UILayoutLength::Px(y);
    return style;
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(
    Platform::WindowId window, UI::UIRoutedPointerEventKind kind, u64 sequence,
    UI::UILogicalPoint position) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] const UI::UISemanticsEntry*
findSemantics(UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found != view.end() ? &*found : nullptr;
}

[[nodiscard]] const UI::UICommittedLayoutEntry*
findLayout(UI::UICommittedLayoutView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UICommittedLayoutEntry& entry) {
            return entry.node == node;
        });
    return found != view.end() ? &*found : nullptr;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

struct TooltipPair final {
    UI::UINodeId anchor{};
    UI::UINodeId tooltip{};
};

[[nodiscard]] TooltipPair createPair(
    UI::UITreeUpdater& updater, UI::UINodeId parent,
    UI::UITooltipConfig config = {},
    UI::UILayoutStyle anchorLayout = overlay(20.0F, 20.0F, 40.0F, 24.0F),
    UI::UILayoutStyle tooltipLayout = fixedSize(80.0F, 24.0F),
    std::string_view tooltipText = "Tooltip help")
{
    auto anchor = updater.createElement(
        parent, UI::makeButtonElement("Anchor", anchorLayout));
    auto tooltip = updater.createElement(
        parent, UI::makeTooltipElement(tooltipText, config, tooltipLayout));
    EXPECT_TRUE(anchor.has_value()) << (anchor ? "" : anchor.error().message);
    EXPECT_TRUE(tooltip.has_value()) << (tooltip ? "" : tooltip.error().message);
    if (!anchor || !tooltip)
    {
        return {};
    }
    EXPECT_TRUE(updater.setTooltipAnchor(*tooltip, *anchor).has_value());
    return {.anchor = *anchor, .tooltip = *tooltip};
}

class UITooltipTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(4);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
        UI::UIContextCapacityConfig capacities = {})
    {
        capacities.nodeCapacity =
            capacities.nodeCapacity == UI::UIContextCapacityConfig::DefaultNodeCapacity
                ? 32
                : capacities.nodeCapacity;
        capacities.rootCapacity =
            capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity
                ? 2
                : capacities.rootCapacity;
        if (capacities.paintSnapshotCapacity == 0)
        {
            capacities.paintSnapshotCapacity = capacities.nodeCapacity * 32U;
        }
        capacities.applyDefaultProductChrome = false;
        auto result = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : nullptr;
    }

    [[nodiscard]] static UI::UIRootOwner createRoot(UI::UIContext& context)
    {
        auto result = context.authoring().rootBuilder().createRoot();
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UIRootOwner{};
    }

    [[nodiscard]] static UI::UITreeUpdater createUpdater(
        UI::UIContext& context, UI::UIRootOwner& root)
    {
        auto result = context.authoring().treeUpdater(root);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? std::move(*result) : UI::UITreeUpdater{};
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UITooltipTest, RecipePublishesDedicatedNonInteractiveContract)
{
    constexpr UI::UITooltipConfig config{
        .placement = UI::UITooltipPlacement::Left,
        .anchorGap = 9.0F,
        .viewportMargin = 11.0F,
        .initialDelay = Core::Duration{0.25},
        .reshowDelay = Core::Duration{0.05},
        .dismissDelay = Core::Duration{0.15},
        .triggers = UI::UITooltipTrigger::Manual,
    };
    constexpr UI::UIElementDescriptor recipe =
        UI::makeTooltipElement("Help", config, fixedSize(90.0F, 28.0F));
    static_assert(recipe.tooltip.has_value());
    static_assert(recipe.layout.placement == UI::UILayoutPlacement::Overlay);
    static_assert(recipe.pointerHitPolicy == UI::UIPointerHitPolicy::Ignore);
    static_assert(recipe.semantics.mode == UI::UISemanticsMode::Exclude);
    static_assert(recipe.semantics.actions == UI::UISemanticsAction::None);
    static_assert(recipe.behaviors == UI::UIElementBehavior::None);
    static_assert(*recipe.tooltip == config);

    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    auto anchor = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Anchor", overlay(10, 10, 50, 24)));
    auto tooltip = updater.createElement(root.rootNodeId(), recipe);
    ASSERT_TRUE(anchor.has_value());
    ASSERT_TRUE(tooltip.has_value()) << tooltip.error().message;
    assertOk(updater.setTooltipAnchor(*tooltip, *anchor));

    UI::UILayoutStyle invalidLayout = fixedSize(90.0F, 28.0F);
    Core::Status invalid = updater.setLayoutStyle(*tooltip, invalidLayout);
    ASSERT_FALSE(invalid.has_value());
    invalid = updater.setPointerHitPolicy(
        *tooltip, UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(invalid.has_value());
    invalid = updater.setFocusScopeMode(
        *tooltip, UI::UIFocusScopeMode::Contain);
    ASSERT_FALSE(invalid.has_value());

    auto invalidChild = updater.createElement(
        *tooltip, UI::makeLabelElement("Tooltip child"));
    ASSERT_FALSE(invalidChild.has_value());
    EXPECT_EQ(invalidChild.error().code, UI::UIErrorCode::InvalidParent);

    UI::UIElementDescriptor malformed = recipe;
    malformed.semantics.mode = UI::UISemanticsMode::Publish;
    auto rejected = updater.createElement(root.rootNodeId(), malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    malformed = recipe;
    malformed.tooltip->initialDelay = Core::Duration{-1.0};
    rejected = updater.createElement(root.rootNodeId(), malformed);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST_F(UITooltipTest, AnchorRelationsRejectCyclesCrossRootStaleAndIncompatibleNodes)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto first = createUpdater(*context, firstRoot);
    auto second = createUpdater(*context, secondRoot);

    auto anchor = first.createElement(
        firstRoot.rootNodeId(), UI::makeButtonElement("Anchor"));
    auto tooltip = first.createElement(
        firstRoot.rootNodeId(), UI::makeTooltipElement("Help"));
    auto otherTooltip = first.createElement(
        firstRoot.rootNodeId(), UI::makeTooltipElement("Other"));
    auto foreignAnchor = second.createElement(
        secondRoot.rootNodeId(), UI::makeButtonElement("Foreign"));
    ASSERT_TRUE(anchor && tooltip && otherTooltip && foreignAnchor);

    Core::Status rejected = first.setTooltipAnchor(*tooltip, *tooltip);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    rejected = first.setTooltipAnchor(*tooltip, *foreignAnchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);

    rejected = first.setTooltipAnchor(*tooltip, firstRoot.rootNodeId());
    ASSERT_FALSE(rejected.has_value());
    rejected = first.setTooltipAnchor(*tooltip, *otherTooltip);
    ASSERT_FALSE(rejected.has_value());

    auto panel = first.createElement(
        firstRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panel.has_value());
    auto descendantTooltip = first.createElement(
        *panel, UI::makeTooltipElement("Descendant"));
    ASSERT_TRUE(descendantTooltip.has_value());
    rejected = first.setTooltipAnchor(*descendantTooltip, *panel);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    assertOk(first.setTooltipAnchor(*tooltip, *anchor));
    EXPECT_EQ(first.tooltipAnchor(*tooltip).value(), *anchor);
    rejected = first.setTooltipAnchor(*otherTooltip, *anchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidParent);

    const UI::UINodeId staleAnchor = *anchor;
    assertOk(first.destroy(*anchor));
    EXPECT_FALSE(first.tooltipAnchor(*tooltip).value().hasValue());
    rejected = first.setTooltipAnchor(*tooltip, staleAnchor);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);
    rejected = first.showTooltip(*tooltip);
    ASSERT_FALSE(rejected.has_value());

    assertOk(first.clearTooltipAnchor(*tooltip));
    EXPECT_FALSE(first.tooltipAnchor(*tooltip).value().hasValue());
}

TEST_F(UITooltipTest, HoverUsesInitialDismissAndReshowDelaysFromInjectedClock)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    FakeTooltipClock clock;
    assertOk(context->motion().setMotionClock(&clock));
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UITooltipConfig config{
        .initialDelay = Core::Duration{1.0},
        .reshowDelay = Core::Duration{0.1},
        .dismissDelay = Core::Duration{0.2},
        .triggers = UI::UITooltipTrigger::PointerHover,
    };
    const TooltipPair first = createPair(
        updater, root.rootNodeId(), config, overlay(10, 20, 40, 24));
    const TooltipPair second = createPair(
        updater, root.rootNodeId(), config, overlay(100, 20, 40, 24));
    ASSERT_TRUE(first.anchor.hasValue() && second.anchor.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 1, {.x = 30, .y = 30})).has_value());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
    clock.advance(Core::Duration{0.999});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
    clock.advance(Core::Duration{0.001});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(first.tooltip).value());

    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 2, {.x = 120, .y = 30})).has_value());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    clock.advance(Core::Duration{0.199});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());
    clock.advance(Core::Duration{0.001});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());
    clock.advance(Core::Duration{0.099});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());
    clock.advance(Core::Duration{0.001});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(second.tooltip).value());
}

TEST_F(UITooltipTest, HugeFiniteDelaySaturatesWithoutWrappingTheClockDeadline)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    FakeTooltipClock clock;
    assertOk(context->motion().setMotionClock(&clock));
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const TooltipPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{
            .initialDelay = Core::Duration{1.0e300},
            .reshowDelay = Core::Duration{1.0e300},
            .dismissDelay = Core::Duration{1.0e300},
            .triggers = UI::UITooltipTrigger::PointerHover,
        });
    ASSERT_TRUE(pair.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 1,
        {.x = 30.0F, .y = 30.0F})).has_value());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    clock.advance(Core::Duration{3600.0});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(pair.tooltip).value());
}

TEST_F(UITooltipTest, KeyboardFocusAndManualTriggersUseTheirOwnPolicies)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    FakeTooltipClock clock;
    assertOk(context->motion().setMotionClock(&clock));
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    const TooltipPair focusPair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{
            .initialDelay = Core::Duration{0.25},
            .dismissDelay = Core::Duration{0.1},
            .triggers = UI::UITooltipTrigger::KeyboardFocus,
        },
        overlay(10, 10, 50, 24));
    const TooltipPair manualPair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{
            .initialDelay = Core::Duration{10.0},
            .reshowDelay = Core::Duration{10.0},
            .dismissDelay = Core::Duration{10.0},
            .triggers = UI::UITooltipTrigger::Manual,
        },
        overlay(100, 10, 50, 24));
    ASSERT_TRUE(focusPair.anchor.hasValue() && manualPair.anchor.hasValue());
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));

    assertOk(context->input().requestFocus(focusPair.anchor));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    clock.advance(Core::Duration{0.25});
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(focusPair.tooltip).value());

    assertOk(context->input().requestFocus(manualPair.anchor));
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    clock.advance(Core::Duration{0.099});
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(focusPair.tooltip).value());
    clock.advance(Core::Duration{0.001});
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(focusPair.tooltip).value());

    assertOk(updater.showTooltip(manualPair.tooltip));
    EXPECT_FALSE(updater.isTooltipOpen(manualPair.tooltip).value());
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(manualPair.tooltip).value());
    EXPECT_EQ(context->input().defaultActionFocus(), manualPair.anchor);
    assertOk(updater.dismissTooltip(manualPair.tooltip));
    EXPECT_TRUE(updater.isTooltipOpen(manualPair.tooltip).value());
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(manualPair.tooltip).value());

    Core::Status rejected = updater.showTooltip(focusPair.tooltip);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
}

TEST_F(UITooltipTest, PlacementSupportsFourDirectionsAutoFlipAndViewportClamp)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const auto manualConfig = [](UI::UITooltipPlacement placement,
                                 float margin = 8.0F) {
        return UI::UITooltipConfig{
            .placement = placement,
            .anchorGap = 5.0F,
            .viewportMargin = margin,
            .initialDelay = Core::Duration{0.0},
            .reshowDelay = Core::Duration{0.0},
            .dismissDelay = Core::Duration{0.0},
            .triggers = UI::UITooltipTrigger::Manual,
        };
    };

    const TooltipPair above = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Above),
        overlay(80, 60, 20, 20), fixedSize(40, 20), "Above");
    const TooltipPair below = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Below),
        overlay(80, 60, 20, 20), fixedSize(40, 20), "Below");
    const TooltipPair left = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Left),
        overlay(80, 60, 20, 20), fixedSize(40, 20), "Left");
    const TooltipPair right = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Right),
        overlay(80, 60, 20, 20), fixedSize(40, 20), "Right");
    const TooltipPair autoFlip = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Auto),
        overlay(80, 130, 20, 20), fixedSize(40, 20), "Auto");
    const TooltipPair explicitFlip = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Below),
        overlay(130, 130, 20, 20), fixedSize(40, 20), "Flip");
    const TooltipPair clamped = createPair(
        updater, root.rootNodeId(), manualConfig(UI::UITooltipPlacement::Below, 10.0F),
        overlay(0, 50, 20, 20), fixedSize(80, 20), "Clamp");
    ASSERT_TRUE(above.tooltip.hasValue() && below.tooltip.hasValue() &&
                left.tooltip.hasValue() && right.tooltip.hasValue() &&
                autoFlip.tooltip.hasValue() && explicitFlip.tooltip.hasValue() &&
                clamped.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 160.0F}));

    const auto expectPlacement = [&](TooltipPair pair,
                                     UI::UITooltipPlacement placement,
                                     float x, float y) {
        assertOk(updater.showTooltip(pair.tooltip));
        assertOk(context->publication().commitLayout({.width = 200.0F, .height = 160.0F}));
        const UI::UITooltipMetrics metrics = updater.tooltipMetrics(pair.tooltip).value();
        EXPECT_TRUE(metrics.open);
        EXPECT_EQ(metrics.resolvedPlacement, placement);
        EXPECT_FLOAT_EQ(metrics.tooltipRect.x, x);
        EXPECT_FLOAT_EQ(metrics.tooltipRect.y, y);
    };
    expectPlacement(above, UI::UITooltipPlacement::Above, 70.0F, 35.0F);
    expectPlacement(below, UI::UITooltipPlacement::Below, 70.0F, 85.0F);
    expectPlacement(left, UI::UITooltipPlacement::Left, 35.0F, 60.0F);
    expectPlacement(right, UI::UITooltipPlacement::Right, 105.0F, 60.0F);
    expectPlacement(autoFlip, UI::UITooltipPlacement::Above, 70.0F, 105.0F);
    expectPlacement(explicitFlip, UI::UITooltipPlacement::Above, 120.0F, 105.0F);
    expectPlacement(clamped, UI::UITooltipPlacement::Below, 10.0F, 75.0F);
}

TEST_F(UITooltipTest, OnlyOneTooltipIsPublishedAndItDoesNotOwnFocusHitOrBarrier)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UITooltipConfig manual{
        .placement = UI::UITooltipPlacement::Below,
        .anchorGap = 0.0F,
        .initialDelay = Core::Duration{0.0},
        .reshowDelay = Core::Duration{0.0},
        .dismissDelay = Core::Duration{0.0},
        .triggers = UI::UITooltipTrigger::Manual,
    };
    const TooltipPair first = createPair(
        updater, root.rootNodeId(), manual, overlay(20, 10, 40, 20),
        fixedSize(40, 20), "First");
    const TooltipPair second = createPair(
        updater, root.rootNodeId(), manual, overlay(100, 10, 40, 20),
        fixedSize(40, 20), "Second");
    auto underlay = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Underlay", overlay(20, 30, 40, 20)));
    ASSERT_TRUE(underlay.has_value());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    assertOk(context->input().requestFocus(first.anchor));

    assertOk(updater.showTooltip(first.tooltip));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_EQ(context->input().defaultActionFocus(), first.anchor);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());

    const UI::UIPointerHitQueryResult query =
        context->input().queryPointerHit({.x = 40.0F, .y = 40.0F});
    EXPECT_FALSE(query.modalBarrierActive);
    ASSERT_TRUE(query.hasTarget());
    EXPECT_EQ(query.target.node, *underlay);

    assertOk(updater.showTooltip(second.tooltip));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_TRUE(updater.isTooltipOpen(second.tooltip).value());

    assertOk(updater.showTooltip(first.tooltip));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    auto down = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1,
        {.x = 40.0F, .y = 40.0F}));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    ASSERT_TRUE(down->hasRoutedTarget());
    EXPECT_EQ(down->routedTarget.node, *underlay);
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), *underlay);
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
}

TEST_F(UITooltipTest, InputVisibilityEnableDestroyAndModalBarriersDismiss)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const TooltipPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{.triggers = UI::UITooltipTrigger::Manual});
    ASSERT_TRUE(pair.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    const auto show = [&] {
        assertOk(updater.showTooltip(pair.tooltip));
        assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
        EXPECT_TRUE(updater.isTooltipOpen(pair.tooltip).value());
    };
    const auto expectClosed = [&] {
        assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
        EXPECT_FALSE(updater.isTooltipOpen(pair.tooltip).value());
    };

    show();
    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Wheel, 1,
        {.x = 180, .y = 100})).has_value());
    expectClosed();

    show();
    auto text = context->text().routeTextInput(
        window, Platform::PlatformFrameId{2}, 2, "x");
    ASSERT_TRUE(text.has_value());
    expectClosed();

    show();
    assertOk(updater.setEnabled(pair.anchor, false));
    expectClosed();
    assertOk(updater.setEnabled(pair.anchor, true));

    show();
    UI::UILayoutStyle hidden = overlay(20, 20, 40, 24);
    hidden.visibility = UI::UIVisibility::Hidden;
    assertOk(updater.setLayoutStyle(pair.anchor, hidden));
    expectClosed();
    assertOk(updater.setLayoutStyle(pair.anchor, overlay(20, 20, 40, 24)));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    show();
    UI::UILayoutStyle collapsed = overlay(20, 20, 40, 24);
    collapsed.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(pair.anchor, collapsed));
    expectClosed();
    assertOk(updater.setLayoutStyle(pair.anchor, overlay(20, 20, 40, 24)));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    show();
    auto modal = updater.createElement(
        root.rootNodeId(), UI::makeModalElement(overlay(80, 40, 80, 60)));
    ASSERT_TRUE(modal.has_value());
    expectClosed();
    EXPECT_EQ(context->input().activeModal(), *modal);

    assertOk(updater.destroy(*modal));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    show();
    assertOk(updater.destroy(pair.anchor));
    expectClosed();
    EXPECT_FALSE(updater.tooltipAnchor(pair.tooltip).value().hasValue());
}

TEST_F(UITooltipTest, AccessibilityUsesTooltipTextOnlyAsAnchorDescriptionFallback)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    UI::UIElementDescriptor implicit =
        UI::makeButtonElement("Implicit", overlay(10, 10, 70, 24));
    UI::UIElementDescriptor explicitDescription =
        UI::makeButtonElement("Explicit", overlay(90, 10, 70, 24));
    explicitDescription.semantics.description = "Authored description";
    auto implicitAnchor = updater.createElement(root.rootNodeId(), implicit);
    auto explicitAnchor = updater.createElement(root.rootNodeId(), explicitDescription);
    auto implicitTooltip = updater.createElement(
        root.rootNodeId(), UI::makeTooltipElement("Fallback help"));
    auto explicitTooltip = updater.createElement(
        root.rootNodeId(), UI::makeTooltipElement("Must not replace"));
    ASSERT_TRUE(implicitAnchor && explicitAnchor && implicitTooltip && explicitTooltip);
    assertOk(updater.setTooltipAnchor(*implicitTooltip, *implicitAnchor));
    assertOk(updater.setTooltipAnchor(*explicitTooltip, *explicitAnchor));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UISemanticsEntry* implicitEntry =
        findSemantics(context->publication().committedSemantics(), *implicitAnchor);
    const UI::UISemanticsEntry* explicitEntry =
        findSemantics(context->publication().committedSemantics(), *explicitAnchor);
    ASSERT_NE(implicitEntry, nullptr);
    ASSERT_NE(explicitEntry, nullptr);
    EXPECT_EQ(implicitEntry->description, "Fallback help");
    EXPECT_EQ(explicitEntry->description, "Authored description");
    EXPECT_EQ(findSemantics(context->publication().committedSemantics(), *implicitTooltip), nullptr);
    EXPECT_EQ(findSemantics(context->publication().committedSemantics(), *explicitTooltip), nullptr);
}

TEST_F(UITooltipTest, FailedCommitKeepsLastPublishedMetricsAndSnapshots)
{
    auto context = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .layoutSnapshotCapacity = 3,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const TooltipPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{
            .initialDelay = Core::Duration{0.0},
            .reshowDelay = Core::Duration{0.0},
            .dismissDelay = Core::Duration{0.0},
            .triggers = UI::UITooltipTrigger::Manual,
        });
    ASSERT_TRUE(pair.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    assertOk(updater.showTooltip(pair.tooltip));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    const UI::UITooltipMetrics published = updater.tooltipMetrics(pair.tooltip).value();
    ASSERT_TRUE(published.open);
    const u64 layoutRevision = context->publication().committedLayout().layoutRevision();
    const u64 paintRevision = context->publication().committedPaint().paintRevision();
    const u64 semanticsRevision = context->publication().committedSemantics().semanticsRevision();

    assertOk(updater.dismissTooltip(pair.tooltip));
    auto overflow = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(10, 10)));
    ASSERT_TRUE(overflow.has_value());
    Core::Status failed =
        context->publication().commitLayout({.width = 200.0F, .height = 100.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(updater.tooltipMetrics(pair.tooltip).value(), published);
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->publication().committedPaint().paintRevision(), paintRevision);
    EXPECT_EQ(context->publication().committedSemantics().semanticsRevision(), semanticsRevision);

    assertOk(updater.destroy(*overflow));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(pair.tooltip).value());
}

TEST_F(UITooltipTest, FailedCommitRollsBackClockDrivenReshowState)
{
    auto context = createContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .layoutSnapshotCapacity = 5,
    });
    ASSERT_NE(context, nullptr);
    FakeTooltipClock clock;
    assertOk(context->motion().setMotionClock(&clock));
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UITooltipConfig config{
        .initialDelay = Core::Duration{0.0},
        .reshowDelay = Core::Duration{1.0},
        .dismissDelay = Core::Duration{0.0},
        .triggers = UI::UITooltipTrigger::PointerHover,
    };
    const TooltipPair first = createPair(
        updater, root.rootNodeId(), config, overlay(10, 20, 40, 24));
    const TooltipPair second = createPair(
        updater, root.rootNodeId(), config, overlay(100, 20, 40, 24));
    ASSERT_TRUE(first.tooltip.hasValue() && second.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 1,
        {.x = 30.0F, .y = 30.0F})).has_value());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    ASSERT_TRUE(updater.isTooltipOpen(first.tooltip).value());

    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 2,
        {.x = 120.0F, .y = 30.0F})).has_value());
    auto overflow = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(10.0F, 10.0F)));
    ASSERT_TRUE(overflow.has_value());
    Core::Status failed =
        context->publication().commitLayout({.width = 200.0F, .height = 120.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());

    clock.advance(Core::Duration{1.0});
    assertOk(updater.destroy(*overflow));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(first.tooltip).value());
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());

    clock.advance(Core::Duration{0.999});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(second.tooltip).value());
    clock.advance(Core::Duration{0.001});
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(second.tooltip).value());
}

TEST_F(UITooltipTest, DestroyGenerationReuseAndRootReleaseLeaveNoAnchorEdges)
{
    auto context = createContext({.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    TooltipPair pair = createPair(
        updater, root.rootNodeId(),
        UI::UITooltipConfig{.triggers = UI::UITooltipTrigger::Manual});
    ASSERT_TRUE(pair.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}));
    assertOk(updater.showTooltip(pair.tooltip));
    assertOk(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}));

    const UI::UINodeId destroyedAnchor = pair.anchor;
    assertOk(updater.destroy(pair.anchor));
    auto replacement = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Replacement"));
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(*replacement, destroyedAnchor);
    EXPECT_FALSE(updater.tooltipAnchor(pair.tooltip).value().hasValue());
    assertOk(updater.setTooltipAnchor(pair.tooltip, *replacement));
    EXPECT_EQ(updater.tooltipAnchor(pair.tooltip).value(), *replacement);

    const UI::UINodeId oldTooltip = pair.tooltip;
    assertOk(updater.destroy(pair.tooltip));
    auto replacementTooltip = updater.createElement(
        root.rootNodeId(), UI::makeTooltipElement(
            "Replacement help",
            UI::UITooltipConfig{.triggers = UI::UITooltipTrigger::Manual}));
    ASSERT_TRUE(replacementTooltip.has_value());
    EXPECT_NE(*replacementTooltip, oldTooltip);
    assertOk(updater.setTooltipAnchor(*replacementTooltip, *replacement));

    root.reset();
    EXPECT_EQ(context->liveNodeCount(), 0U);
    auto stale = updater.tooltipAnchor(*replacementTooltip);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, UI::UIErrorCode::RootRequired);

    auto newRoot = createRoot(*context);
    auto newUpdater = createUpdater(*context, newRoot);
    const TooltipPair newPair = createPair(
        newUpdater, newRoot.rootNodeId(),
        UI::UITooltipConfig{.triggers = UI::UITooltipTrigger::Manual});
    ASSERT_TRUE(newPair.tooltip.hasValue());
    assertOk(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}));
    assertOk(newUpdater.showTooltip(newPair.tooltip));
    assertOk(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}));
    EXPECT_TRUE(newUpdater.isTooltipOpen(newPair.tooltip).value());
}

} // namespace
} // namespace Tina::Tests
