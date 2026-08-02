#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UI.hpp>

#include <chrono>
#include <memory>

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

} // namespace
} // namespace Tina::Tests
