#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UI.hpp>

#include <chrono>
#include <memory>
#include <thread>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class FakeMotionClock final : public Core::IMonotonicClock {
  public:
    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void advance(Core::Duration delta) noexcept
    {
        now_ += std::chrono::duration_cast<Core::MonotonicDuration>(delta);
    }

    void set(Core::MonotonicTimePoint point) noexcept
    {
        now_ = point;
    }

  private:
    Core::MonotonicTimePoint now_{};
};

[[nodiscard]] Platform::WindowId makeMotionWindow()
{
    static auto windows = [] {
        auto pool = WindowPool::Create(16);
        EXPECT_TRUE(pool.has_value());
        return std::make_unique<WindowPool>(std::move(*pool));
    }();
    auto id = windows->tryEmplace(1);
    EXPECT_TRUE(id.has_value());
    return id ? *id : Platform::WindowId{};
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createMotionContext(usize motionTracks = 4)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .motionTrackCapacity = motionTracks,
        .applyDefaultProductChrome = false,
    };
    auto context = UI::UIContext::Create(makeMotionWindow(), config);
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    return context ? std::move(*context) : nullptr;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] const UI::UICommittedPaintEntry*
findSolidPaint(UI::UICommittedPaintView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : view.entries())
    {
        if (entry.node == node && entry.kind == UI::UICommittedPaintKind::SolidQuad)
        {
            return &entry;
        }
    }
    return nullptr;
}

TEST(UIMotionTests, EasingAndLerpHelpersAreDeterministic)
{
    EXPECT_FLOAT_EQ(UI::evaluateUIEasing(UI::UIEasing::Linear, 0.5F), 0.5F);
    EXPECT_GT(UI::evaluateUIEasing(UI::UIEasing::EaseOut, 0.5F), 0.5F);
    EXPECT_TRUE(UI::isPaintOnlyAnimatableProperty(UI::UIAnimatableProperty::BackgroundColor));
    const auto mid =
        UI::lerpStraightSrgba8(UI::rgba8(0, 0, 0, 255), UI::rgba8(100, 0, 0, 255), 0.5F);
    EXPECT_EQ(mid.red, 50);
    EXPECT_EQ(mid.alpha, 255);
}

TEST(UIMotionTests, BackgroundColorTransitionSamplesWithoutLayoutDirty)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    ASSERT_NE(findSolidPaint(context->committedPaint(), *node), nullptr);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->solidFill,
              UI::premultiply(UI::rgba8(0, 0, 0, 255)));

    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
        .delay = Core::Duration{0.0},
        .easing = UI::UIEasing::Linear,
    };
    ASSERT_TRUE(
        context->beginBackgroundColorTransition(*node, UI::rgba8(100, 0, 0, 255), spec).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_TRUE(context->statistics().paintDirty);
    EXPECT_FALSE(context->statistics().layoutDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* midPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(midPaint, nullptr);
    EXPECT_EQ(midPaint->solidFill, UI::premultiply(UI::rgba8(50, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.lastSampledTrackCount, 1U);
    EXPECT_FALSE(context->statistics().layoutDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* endPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(endPaint, nullptr);
    EXPECT_EQ(endPaint->solidFill, UI::premultiply(UI::rgba8(100, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
}

TEST(UIMotionTests, ZeroActiveTracksDoNotForcePaintDirty)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    EXPECT_FALSE(context->statistics().paintDirty);
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    EXPECT_FALSE(context->statistics().paintDirty);
    EXPECT_EQ(context->statistics().motion.lastSampledTrackCount, 0U);
}

TEST(UIMotionTests, ReducedMotionSnapsWithoutActiveTrack)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(10, 20, 30, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());

    ASSERT_TRUE(context->setReducedMotion(true).has_value());
    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{1.0},
        .easing = UI::UIEasing::EaseInOut,
    };
    ASSERT_TRUE(
        context->beginBackgroundColorTransition(*node, UI::rgba8(200, 100, 50, 255), spec).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());
    const auto* paint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(200, 100, 50, 255)));
}

TEST(UIMotionTests, CapacityFailureLeavesExistingTracksIntact)
{
    auto context = createMotionContext(1);
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor a = UI::makePanelElement(fixedSize(10.0F, 10.0F));
    a.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    UI::UIElementDescriptor b = UI::makePanelElement(fixedSize(10.0F, 10.0F));
    b.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto nodeA = updater.createElement(root.rootNodeId(), a);
    const auto nodeB = updater.createElement(root.rootNodeId(), b);
    ASSERT_TRUE(nodeA.has_value());
    ASSERT_TRUE(nodeB.has_value());

    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{1.0},
    };
    ASSERT_TRUE(
        context->beginBackgroundColorTransition(*nodeA, UI::rgba8(255, 0, 0, 255), spec).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    const Core::Status overflow =
        context->beginBackgroundColorTransition(*nodeB, UI::rgba8(0, 255, 0, 255), spec);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
}

TEST(UIMotionTests, CornerRadiusAndOpacityTransitionsArePaintOnly)
{
    auto context = createMotionContext(8);
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(40, 40, 40, 255), 0.0F);
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    UI::UITransitionSpec radiusSpec{
        .property = UI::UIAnimatableProperty::CornerRadius,
        .duration = Core::Duration{0.100},
        .easing = UI::UIEasing::Linear,
    };
    UI::UITransitionSpec opacitySpec{
        .property = UI::UIAnimatableProperty::Opacity,
        .duration = Core::Duration{0.100},
        .easing = UI::UIEasing::Linear,
    };
    ASSERT_TRUE(context->beginCornerRadiusTransition(*node, 8.0F, radiusSpec).has_value());
    ASSERT_TRUE(context->beginOpacityTransition(*node, 0.0F, opacitySpec).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 2U);
    EXPECT_FALSE(context->statistics().layoutDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* mid = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(mid, nullptr);
    // Opacity 0.5 on (40,40,40,255) -> premultiplied ~ (20,20,20,128)
    EXPECT_EQ(mid->solidFill.alpha, 128);
    EXPECT_FALSE(context->statistics().layoutDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    const auto* end = findSolidPaint(context->committedPaint(), *node);
    // Opacity 0 -> transparent fill may omit solid entry or alpha 0.
    if (end != nullptr)
    {
        EXPECT_EQ(end->solidFill.alpha, 0);
    }
}

TEST(UIMotionTests, VisualOffsetShiftsPaintWorldRectNotLayout)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(10, 20, 30, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 100.0F}).has_value());
    const auto* before = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(before, nullptr);
    const float beforeX = before->worldRect.x;
    const float beforeY = before->worldRect.y;

    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::VisualOffset,
        .duration = Core::Duration{0.0},
    };
    ASSERT_TRUE(context->beginVisualOffsetTransition(*node, 12.0F, 8.0F, spec).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 100.0F}).has_value());
    const auto* after = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->worldRect.x, beforeX + 12.0F);
    EXPECT_FLOAT_EQ(after->worldRect.y, beforeY + 8.0F);
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_FALSE(context->statistics().hitDirty);
}

TEST(UIMotionTests, StyleStateChangeCanDriveBackgroundColorTransition)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .styleClassCapacity = 4,
        .styleTokenCapacity = 4,
        .styleRuleCapacity = 8,
        .styleBucketCapacity = 8,
        .styleRulesPerBucketCapacity = 4,
        .motionTrackCapacity = 4,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto styleClass = context->registerStyleClass();
    ASSERT_TRUE(styleClass.has_value()) << styleClass.error().message;
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = *styleClass,
            .color = UI::rgba8(0, 0, 0, 255),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = *styleClass,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = UI::rgba8(100, 0, 0, 255),
        },
    };
    ASSERT_TRUE(context->installStyleSheet(rules).has_value());
    ASSERT_TRUE(context
                    ->setStyleBackgroundColorTransition(UI::UITransitionSpec{
                        .property = UI::UIAnimatableProperty::BackgroundColor,
                        .duration = Core::Duration{0.100},
                        .easing = UI::UIEasing::Linear,
                    })
                    .has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor button = UI::makeButtonElement("Go", fixedSize(80.0F, 40.0F));
    button.visual.styleRole = UI::UIStyleRoleId::ButtonPrimary;
    const UI::UIStyleClassId classId = *styleClass;
    button.visual.styleClasses = std::span(&classId, 1);
    const auto node = updater.createElement(root.rootNodeId(), button);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    ASSERT_NE(findSolidPaint(context->committedPaint(), *node), nullptr);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->solidFill, UI::premultiply(UI::rgba8(0, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);

    const UI::UIPointerInputEvent hoverInput{
        .platformFrame = Platform::PlatformFrameId{1},
        .transitionOrdinal = 0,
        .sourceSequence = 1,
        .window = context->ownerWindow(),
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::Move,
        .position = {.x = 40.0F, .y = 20.0F},
        .delta = {.x = 1.0F, .y = 0.0F},
        .button = Platform::PointerButton::Primary,
    };
    auto route = context->routePointerInput(hoverInput);
    ASSERT_TRUE(route.has_value()) << route.error().message;
    // Style→motion retarget runs during paint rebuild on commitLayout.
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
    EXPECT_FALSE(context->statistics().layoutDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const auto* mid = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(mid->solidFill, UI::premultiply(UI::rgba8(50, 0, 0, 255)));

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const auto* completed = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(completed, nullptr);
    EXPECT_EQ(completed->solidFill, UI::premultiply(UI::rgba8(100, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);

    const UI::UIPointerInputEvent leaveInput{
        .platformFrame = Platform::PlatformFrameId{2},
        .transitionOrdinal = 0,
        .sourceSequence = 2,
        .window = context->ownerWindow(),
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::Move,
        .position = {.x = 140.0F, .y = 70.0F},
        .delta = {.x = 100.0F, .y = 50.0F},
        .button = Platform::PointerButton::Primary,
    };
    route = context->routePointerInput(leaveInput);
    ASSERT_TRUE(route.has_value()) << route.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);

    ASSERT_TRUE(updater.setBoxPaint(*node, UI::makeSolidBox(UI::rgba8(0, 80, 0, 255))).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const auto* overridden = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(overridden, nullptr);
    EXPECT_EQ(overridden->solidFill, UI::premultiply(UI::rgba8(0, 80, 0, 255)));
}

TEST(UIMotionTests, ElementCreationReservesAgainstAuthoredStyleBinding)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 16,
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .motionTrackCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .color = UI::rgba8(0, 0, 0, 255),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = UI::rgba8(100, 0, 0, 255),
        },
    };
    ASSERT_TRUE(context->installStyleSheet(rules).has_value());
    ASSERT_TRUE(context
                    ->setStyleBackgroundColorTransition(UI::UITransitionSpec{
                        .property = UI::UIAnimatableProperty::BackgroundColor,
                        .duration = Core::Duration{0.100},
                        .easing = UI::UIEasing::Linear,
                    })
                    .has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor button = UI::makeButtonElement("No motion", fixedSize(80.0F, 40.0F));
    button.visual.styleRole = UI::UIStyleRoleId::None;
    auto unbound = updater.createElement(root.rootNodeId(), button);
    ASSERT_TRUE(unbound.has_value()) << unbound.error().message;
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);

    const Core::Status initialCommit = context->commitLayout({.width = 160.0F, .height = 80.0F});
    ASSERT_TRUE(initialCommit.has_value()) << initialCommit.error().message;
    const UI::UIPointerInputEvent hoverUnbound{
        .platformFrame = Platform::PlatformFrameId{1},
        .transitionOrdinal = 0,
        .sourceSequence = 1,
        .window = context->ownerWindow(),
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::Move,
        .position = {.x = 40.0F, .y = 20.0F},
        .delta = {.x = 1.0F, .y = 0.0F},
        .button = Platform::PointerButton::Primary,
    };
    ASSERT_TRUE(context->routePointerInput(hoverUnbound).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);

    button.visual.styleRole = UI::UIStyleRoleId::ButtonPrimary;
    auto firstBound = updater.createElement(root.rootNodeId(), button);
    ASSERT_TRUE(firstBound.has_value()) << firstBound.error().message;
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);

    button.text = "Capacity full";
    auto secondBound = updater.createElement(root.rootNodeId(), button);
    ASSERT_FALSE(secondBound.has_value());
    EXPECT_EQ(secondBound.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);

    ASSERT_TRUE(updater.setStyleRole(*firstBound, UI::UIStyleRoleId::None).has_value());
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);

    auto reusedReservation = updater.createElement(root.rootNodeId(), button);
    ASSERT_TRUE(reusedReservation.has_value()) << reusedReservation.error().message;
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
    ASSERT_TRUE(updater.destroy(*reusedReservation).has_value());
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);

    ASSERT_TRUE(updater.setStyleRole(*firstBound, UI::UIStyleRoleId::ButtonPrimary).has_value());
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
    ASSERT_TRUE(updater.destroy(*firstBound).has_value());
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);
}

TEST(UIMotionTests, StyleTransitionEnablePreflightsReservationsAtomically)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .styleClassCapacity = 2,
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .motionTrackCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);

    auto styleClass = context->registerStyleClass();
    ASSERT_TRUE(styleClass.has_value()) << styleClass.error().message;
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = *styleClass,
            .color = UI::rgba8(0, 0, 0, 255),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = *styleClass,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = UI::rgba8(100, 0, 0, 255),
        },
    };
    ASSERT_TRUE(context->installStyleSheet(rules).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    const UI::UIStyleClassId classId = *styleClass;
    UI::UIElementDescriptor button = UI::makeButtonElement("One", fixedSize(80.0F, 40.0F));
    button.visual.styleRole = UI::UIStyleRoleId::ButtonPrimary;
    button.visual.styleClasses = std::span(&classId, 1);
    ASSERT_TRUE(updater.createElement(root.rootNodeId(), button).has_value());
    button.text = "Two";
    ASSERT_TRUE(updater.createElement(root.rootNodeId(), button).has_value());

    const UI::UITransitionSpec transition{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
        .easing = UI::UIEasing::Linear,
    };
    const Core::Status enable = context->setStyleBackgroundColorTransition(transition);
    ASSERT_FALSE(enable.has_value());
    EXPECT_EQ(enable.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->styleBackgroundColorTransition().duration.count(), 0.0);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
}

TEST(UIMotionTests, StyleTransitionEnableDrainsDeferredRootsBeforeCapacityPreflight)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 2,
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .motionTrackCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .color = UI::rgba8(0, 0, 0, 255),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = UI::rgba8(100, 0, 0, 255),
        },
    };
    ASSERT_TRUE(context->installStyleSheet(rules).has_value());

    auto firstRootResult = context->rootBuilder().createRoot();
    auto deferredRootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(firstRootResult.has_value());
    ASSERT_TRUE(deferredRootResult.has_value());
    UI::UIRootOwner firstRoot = std::move(*firstRootResult);
    UI::UIRootOwner deferredRoot = std::move(*deferredRootResult);

    UI::UIElementDescriptor button = UI::makeButtonElement("Bound", fixedSize(80.0F, 40.0F));
    button.visual.styleRole = UI::UIStyleRoleId::ButtonPrimary;
    auto firstUpdaterResult = context->treeUpdater(firstRoot);
    auto deferredUpdaterResult = context->treeUpdater(deferredRoot);
    ASSERT_TRUE(firstUpdaterResult.has_value());
    ASSERT_TRUE(deferredUpdaterResult.has_value());
    const auto firstNode = firstUpdaterResult->createElement(firstRoot.rootNodeId(), button);
    const auto deferredNode = deferredUpdaterResult->createElement(deferredRoot.rootNodeId(), button);
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(deferredNode.has_value());

    std::thread releaseThread([ownedRoot = std::move(deferredRoot)]() mutable {
        ownedRoot.reset();
    });
    releaseThread.join();

    ASSERT_TRUE(context
                    ->setStyleBackgroundColorTransition(UI::UITransitionSpec{
                        .property = UI::UIAnimatableProperty::BackgroundColor,
                        .duration = Core::Duration{0.100},
                        .easing = UI::UIEasing::Linear,
                    })
                    .has_value());
    EXPECT_TRUE(context->contains(*firstNode));
    EXPECT_FALSE(context->contains(*deferredNode));
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
}

} // namespace
} // namespace Tina::Tests
