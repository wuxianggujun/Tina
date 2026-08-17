#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UI.hpp>

#include <chrono>
#include <memory>
#include <memory_resource>
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

class CountingMemoryResource final : public std::pmr::memory_resource {
  public:
    explicit CountingMemoryResource(
        std::pmr::memory_resource& upstream = *std::pmr::get_default_resource()) noexcept
        : upstream_(&upstream)
    {
    }

    [[nodiscard]] usize allocationCalls() const noexcept
    {
        return allocationCalls_;
    }

  private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCalls_;
        return upstream_->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        upstream_->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_ = nullptr;
    usize allocationCalls_ = 0;
};

[[nodiscard]] Platform::WindowId makeMotionWindow()
{
    static auto windows = [] {
        // IDs remain live for the process lifetime because these unit contexts
        // intentionally do not own the synthetic WindowPool. Keep enough slots
        // for the complete suite rather than making later tests order-dependent.
        auto pool = WindowPool::Create(64);
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

[[nodiscard]] const UI::UICommittedLayoutEntry*
findLayoutEntry(UI::UICommittedLayoutView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UICommittedHitEntry*
findHitEntry(UI::UICommittedHitView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedHitEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] UI::UILayoutStyle overlayStyle(
    float x, float y, float width, float height) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(x);
    style.overlay.offset.y = UI::UILayoutLength::Px(y);
    return style;
}

TEST(UIMotionTests, EasingAndLerpHelpersAreDeterministic)
{
    EXPECT_FLOAT_EQ(UI::evaluateUIEasing(UI::UIEasing::Linear, 0.5F), 0.5F);
    EXPECT_GT(UI::evaluateUIEasing(UI::UIEasing::EaseOut, 0.5F), 0.5F);
    EXPECT_TRUE(UI::isPaintOnlyAnimatableProperty(UI::UIAnimatableProperty::BackgroundColor));
    EXPECT_FALSE(UI::isPaintOnlyAnimatableProperty(UI::UIAnimatableProperty::LayoutWidth));
    EXPECT_TRUE(UI::isLayoutAnimatableProperty(UI::UIAnimatableProperty::LayoutHeight));
    EXPECT_TRUE(UI::isTimelineAnimatableProperty(UI::UIAnimatableProperty::LayoutOffset));
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

TEST(UIMotionTests, DirectMotionCompletionDoesNotConsumeDirtyQueueCapacity)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 1,
        .paintSnapshotCapacity = 4,
        .motionTrackCapacity = 2,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor childPanel = UI::makePanelElement(fixedSize(10.0F, 10.0F));
    childPanel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto child = updater.createElement(root.rootNodeId(), childPanel);
    ASSERT_TRUE(child.has_value()) << child.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());
    ASSERT_TRUE(updater
                    .setBoxPaint(root.rootNodeId(),
                                 UI::makeSolidBox(UI::rgba8(0, 0, 0, 255)))
                    .has_value());
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());

    const UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
    };
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        root.rootNodeId(), UI::rgba8(100, 0, 0, 255), spec)
                    .has_value());
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        *child, UI::rgba8(0, 100, 0, 255), spec)
                    .has_value());
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());
    ASSERT_EQ(context->statistics().motion.activeTrackCount, 2U);
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 0U);

    clock.advance(Core::Duration{0.100});
    ASSERT_TRUE(context->commitLayout({.width = 40.0F, .height = 40.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 0U);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);
    const auto* rootPaint = findSolidPaint(context->committedPaint(), root.rootNodeId());
    const auto* childPaint = findSolidPaint(context->committedPaint(), *child);
    ASSERT_NE(rootPaint, nullptr);
    ASSERT_NE(childPaint, nullptr);
    EXPECT_EQ(rootPaint->solidFill, UI::premultiply(UI::rgba8(100, 0, 0, 255)));
    EXPECT_EQ(childPaint->solidFill, UI::premultiply(UI::rgba8(0, 100, 0, 255)));
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

TEST(UIMotionTests, CornerRadiusTransitionRejectsAsymmetricAuthoredRadiiAtomically)
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
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(40, 40, 40, 255));
    panel.visual.boxPaint->cornerRadii = {
        .topLeft = 8.0F,
        .topRight = 6.0F,
        .bottomRight = 4.0F,
        .bottomLeft = 2.0F,
    };
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const auto* before = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(before, nullptr);
    const UI::UILogicalCornerRadii committedRadii = before->cornerRadii;
    const u64 paintRevision = context->committedPaint().paintRevision();
    const usize activeTrackCount = context->statistics().motion.activeTrackCount;
    const UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::CornerRadius,
        .duration = Core::Duration{0.100},
        .easing = UI::UIEasing::Linear,
    };

    const Core::Status rejected =
        context->beginCornerRadiusTransition(*node, 12.0F, spec);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidStyle);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, activeTrackCount);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    const auto* after = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->cornerRadii, committedRadii);
}

TEST(UIMotionTests, CornerRadiusTimelineUsesExplicitUniformKeyframeFromAsymmetricPaint)
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
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(40, 40, 40, 255));
    panel.visual.boxPaint->cornerRadii = {
        .topLeft = 8.0F,
        .topRight = 6.0F,
        .bottomRight = 4.0F,
        .bottomLeft = 2.0F,
    };
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* authored = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(authored, nullptr);
    EXPECT_EQ(authored->cornerRadii, panel.visual.boxPaint->cornerRadii);

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(3.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(11.0F)},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.100}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const auto* firstFrame = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(firstFrame, nullptr);
    EXPECT_EQ(firstFrame->cornerRadii, UI::UILogicalCornerRadii::uniform(3.0F));

    ASSERT_TRUE(context->cancelTimeline(*timeline).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* finalTarget = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(finalTarget, nullptr);
    EXPECT_EQ(finalTarget->cornerRadii, UI::UILogicalCornerRadii::uniform(11.0F));
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

TEST(UIMotionTests, TimelineSamplesTypedTracksWithoutLayoutOrHitRebuild)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 32,
        .timelineCapacity = 2,
        .timelineTrackCapacity = 8,
        .timelineKeyframeCapacity = 16,
        .activeTimelineCapacity = 2,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
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
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());

    const std::array colorFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
    };
    const std::array opacityFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(1.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(0.5F)},
    };
    const std::array radiusFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(8.0F)},
    };
    const std::array offsetFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Offset(0.0F, 0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Offset(12.0F, 4.0F)},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = colorFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::Opacity,
                                .keyframes = opacityFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = radiusFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::VisualOffset,
                                .keyframes = offsetFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .delay = Core::Duration{0.0},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    EXPECT_EQ(context->statistics().motion.timelineCount, 1U);
    EXPECT_EQ(context->statistics().motion.timelineTrackCount, 4U);
    EXPECT_EQ(context->statistics().motion.keyframeCount, 8U);

    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_FALSE(context->statistics().hitDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());
    const auto* middle = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(middle, nullptr);
    // Background 50 with opacity 0.75 is premultiplied with integer truncation.
    EXPECT_EQ(middle->solidFill.red, 37);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);
    EXPECT_GT(context->statistics().motion.lastSampledTimelineTrackCount, 0U);
    EXPECT_GT(context->statistics().motion.lastSampledKeyframeSegmentCount, 0U);
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_FALSE(context->statistics().hitDirty);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    EXPECT_EQ(context->statistics().motion.lastSampledTimelineCount, 1U);
    EXPECT_EQ(context->statistics().motion.lastSampledTimelineTrackCount, 4U);
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    EXPECT_EQ(context->statistics().motion.lastSampledTimelineCount, 0U);
    EXPECT_EQ(context->statistics().motion.lastSampledTimelineTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.lastSampledTimelineLayoutTrackCount, 0U);
    EXPECT_EQ(context->statistics().motion.lastSampledKeyframeSegmentCount, 0U);
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    EXPECT_EQ(context->statistics().motion.timelineTrackCount, 4U);
    const auto* finalPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(finalPaint, nullptr);
    EXPECT_EQ(finalPaint->solidFill.red, 50);
}

TEST(UIMotionTests, LayoutTimelinePublishesLayoutHitAndPaintFromOneSample)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 8,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 4,
        .timelineKeyframeCapacity = 8,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor panel = UI::makePanelElement(overlayStyle(10.0F, 12.0F, 20.0F, 10.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(updater.setPointerHitPolicy(*node, UI::UIPointerHitPolicy::Targetable).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 120.0F, .height = 80.0F}).has_value());

    const std::array widthFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(20.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(60.0F)},
    };
    const std::array heightFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(10.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(30.0F)},
    };
    const std::array layoutOffsetFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Offset(10.0F, 12.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Offset(50.0F, 32.0F)},
    };
    const std::array colorFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutWidth,
                                .keyframes = widthFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutHeight,
                                .keyframes = heightFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutOffset,
                                .keyframes = layoutOffsetFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = colorFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 hitRevision = context->committedHit().hitRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->commitLayout({.width = 120.0F, .height = 80.0F}).has_value());
    const auto* layout = findLayoutEntry(context->committedLayout(), *node);
    const auto* hit = findHitEntry(context->committedHit(), *node);
    const auto* paint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(layout, nullptr);
    ASSERT_NE(hit, nullptr);
    ASSERT_NE(paint, nullptr);
    const UI::UILogicalRect expected{
        .x = 30.0F,
        .y = 22.0F,
        .width = 40.0F,
        .height = 20.0F,
    };
    EXPECT_EQ(layout->worldRect, expected);
    EXPECT_EQ(hit->worldRect, expected);
    EXPECT_EQ(paint->worldRect, expected);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(50, 0, 0, 255)));
    const UI::UIPointerHitQueryResult movedHit = context->queryPointerHit({50.0F, 30.0F});
    ASSERT_TRUE(movedHit.hasTarget());
    EXPECT_EQ(movedHit.target.node, *node);
    EXPECT_FALSE(context->queryPointerHit({15.0F, 15.0F}).hasTarget());
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision + 1U);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision + 1U);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.motion.lastSampledTimelineCount, 1U);
    EXPECT_EQ(statistics.motion.lastSampledTimelineTrackCount, 4U);
    EXPECT_EQ(statistics.motion.lastSampledTimelineLayoutTrackCount, 3U);
    EXPECT_EQ(statistics.motion.lastSampledKeyframeSegmentCount, 4U);
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastHitRebuildCount, 1U);
    EXPECT_EQ(statistics.lastPaintSnapshotRebuildCount, 1U);
    EXPECT_FALSE(statistics.layoutDirty);
    EXPECT_FALSE(statistics.hitDirty);
    EXPECT_FALSE(statistics.paintDirty);
}

TEST(UIMotionTests, MixedLayoutTimelineFailureKeepsPublishedSampleAndRetriesAtAbsoluteTime)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 1,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 2,
        .timelineKeyframeCapacity = 4,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(overlayStyle(10.0F, 12.0F, 20.0F, 10.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(updater.setPointerHitPolicy(*node, UI::UIPointerHitPolicy::Targetable).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 120.0F, .height = 80.0F}).has_value());

    const std::array widthFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(20.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(60.0F)},
    };
    const std::array colorFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutWidth,
                                .keyframes = widthFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = colorFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    ASSERT_TRUE(updater
                    .setBoxPaint(root.rootNodeId(), UI::makeSolidBox(UI::rgba8(1, 2, 3, 255)))
                    .has_value());

    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 hitRevision = context->committedHit().hitRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();
    const auto* publishedLayout = findLayoutEntry(context->committedLayout(), *node);
    const auto* publishedPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(publishedLayout, nullptr);
    ASSERT_NE(publishedPaint, nullptr);
    const UI::UILogicalRect publishedRect = publishedLayout->worldRect;
    const UI::UIPremultipliedRgba8Color publishedFill = publishedPaint->solidFill;

    clock.advance(Core::Duration{0.050});
    const Core::Status rejected = context->commitLayout({.width = 120.0F, .height = 80.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    EXPECT_EQ(findLayoutEntry(context->committedLayout(), *node)->worldRect, publishedRect);
    EXPECT_EQ(findHitEntry(context->committedHit(), *node)->worldRect, publishedRect);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->worldRect, publishedRect);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->solidFill, publishedFill);
    EXPECT_EQ(context->statistics().motion.layoutTimelineCommitFailureCount, 1U);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);
    ASSERT_TRUE(context->isTimelineActive(*timeline).has_value());
    EXPECT_TRUE(*context->isTimelineActive(*timeline));

    ASSERT_TRUE(updater.setBoxPaint(root.rootNodeId(), {}).has_value());
    clock.advance(Core::Duration{0.025});
    ASSERT_TRUE(context->commitLayout({.width = 120.0F, .height = 80.0F}).has_value());
    const auto* recoveredLayout = findLayoutEntry(context->committedLayout(), *node);
    const auto* recoveredHit = findHitEntry(context->committedHit(), *node);
    const auto* recoveredPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(recoveredLayout, nullptr);
    ASSERT_NE(recoveredHit, nullptr);
    ASSERT_NE(recoveredPaint, nullptr);
    EXPECT_FLOAT_EQ(recoveredLayout->worldRect.width, 50.0F);
    EXPECT_EQ(recoveredHit->worldRect, recoveredLayout->worldRect);
    EXPECT_EQ(recoveredPaint->worldRect, recoveredLayout->worldRect);
    EXPECT_EQ(recoveredPaint->solidFill, UI::premultiply(UI::rgba8(75, 0, 0, 255)));
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision + 1U);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision + 1U);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);
    EXPECT_EQ(context->statistics().motion.layoutTimelineCommitFailureCount, 1U);
}

TEST(UIMotionTests, LayoutTimelineLateFailureRollsBackDirectMotionCandidates)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 3,
        .motionTrackCapacity = 2,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 1,
        .timelineKeyframeCapacity = 2,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    const auto createPaintedPanel = [&](float y) {
        UI::UIElementDescriptor panel =
            UI::makePanelElement(overlayStyle(0.0F, y, 20.0F, 10.0F));
        panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
        return updater.createElement(root.rootNodeId(), panel);
    };
    const auto layoutNode = createPaintedPanel(0.0F);
    const auto unfinishedDirectNode = createPaintedPanel(20.0F);
    const auto completingDirectNode = createPaintedPanel(40.0F);
    ASSERT_TRUE(layoutNode.has_value()) << layoutNode.error().message;
    ASSERT_TRUE(unfinishedDirectNode.has_value()) << unfinishedDirectNode.error().message;
    ASSERT_TRUE(completingDirectNode.has_value()) << completingDirectNode.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());

    const std::array widthFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(20.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(60.0F)},
    };
    const std::array layoutTracks{
        UI::UITimelineTrackDesc{.node = *layoutNode,
                                .property = UI::UIAnimatableProperty::LayoutWidth,
                                .keyframes = widthFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .tracks = layoutTracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());

    const UI::UITransitionSpec unfinishedSpec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
    };
    const UI::UITransitionSpec completingSpec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.050},
    };
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        *unfinishedDirectNode, UI::rgba8(200, 0, 0, 255), unfinishedSpec)
                    .has_value());
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        *completingDirectNode, UI::rgba8(0, 100, 0, 255), completingSpec)
                    .has_value());
    ASSERT_TRUE(updater
                    .setBoxPaint(root.rootNodeId(),
                                 UI::makeSolidBox(UI::rgba8(1, 2, 3, 255)))
                    .has_value());

    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 hitRevision = context->committedHit().hitRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();
    clock.advance(Core::Duration{0.050});
    const Core::Status rejected = context->commitLayout({.width = 100.0F, .height = 80.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 2U);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);
    EXPECT_EQ(context->statistics().motion.layoutTimelineCommitFailureCount, 1U);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    const auto* rejectedUnfinishedPaint =
        findSolidPaint(context->committedPaint(), *unfinishedDirectNode);
    const auto* rejectedCompletingPaint =
        findSolidPaint(context->committedPaint(), *completingDirectNode);
    ASSERT_NE(rejectedUnfinishedPaint, nullptr);
    ASSERT_NE(rejectedCompletingPaint, nullptr);
    EXPECT_EQ(rejectedUnfinishedPaint->solidFill,
              UI::premultiply(UI::rgba8(0, 0, 0, 255)));
    EXPECT_EQ(rejectedCompletingPaint->solidFill,
              UI::premultiply(UI::rgba8(0, 0, 0, 255)));

    // A failed candidate must not become the retarget start. At the next
    // half-sample this is 100 from the last committed 0, not 150 from 100.
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        *unfinishedDirectNode, UI::rgba8(200, 0, 0, 255), unfinishedSpec)
                    .has_value());
    ASSERT_TRUE(updater.setBoxPaint(root.rootNodeId(), {}).has_value());
    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 80.0F}).has_value());

    const auto* recoveredLayout = findLayoutEntry(context->committedLayout(), *layoutNode);
    const auto* unfinishedPaint =
        findSolidPaint(context->committedPaint(), *unfinishedDirectNode);
    const auto* completedPaint =
        findSolidPaint(context->committedPaint(), *completingDirectNode);
    ASSERT_NE(recoveredLayout, nullptr);
    ASSERT_NE(unfinishedPaint, nullptr);
    ASSERT_NE(completedPaint, nullptr);
    EXPECT_FLOAT_EQ(recoveredLayout->worldRect.width, 60.0F);
    EXPECT_EQ(unfinishedPaint->solidFill,
              UI::premultiply(UI::rgba8(100, 0, 0, 255)));
    EXPECT_EQ(completedPaint->solidFill,
              UI::premultiply(UI::rgba8(0, 100, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    EXPECT_EQ(context->statistics().motion.layoutTimelineCommitFailureCount, 1U);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision + 1U);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision + 1U);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);
}

TEST(UIMotionTests, LayoutTimelineLateFailureDefersStyleActivationAndKeepsDirectSample)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 3,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 2,
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .motionTrackCapacity = 2,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 1,
        .timelineKeyframeCapacity = 2,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .color = UI::rgba8(0, 0, 0, 255),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .requiredStates = UI::UIStyleState::Disabled,
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

    UI::UIElementDescriptor stylePanel =
        UI::makeButtonElement({}, overlayStyle(0.0F, 0.0F, 20.0F, 10.0F));
    stylePanel.visual.styleRole = UI::UIStyleRoleId::ButtonPrimary;
    const auto styleNode = updater.createElement(root.rootNodeId(), stylePanel);
    ASSERT_TRUE(styleNode.has_value()) << styleNode.error().message;

    UI::UIElementDescriptor directPanel =
        UI::makePanelElement(overlayStyle(0.0F, 20.0F, 20.0F, 10.0F));
    directPanel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto directNode = updater.createElement(root.rootNodeId(), directPanel);
    ASSERT_TRUE(directNode.has_value()) << directNode.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    ASSERT_NE(findSolidPaint(context->committedPaint(), *styleNode), nullptr);
    ASSERT_NE(findSolidPaint(context->committedPaint(), *directNode), nullptr);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);

    const std::array widthFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(20.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(60.0F)},
    };
    const std::array layoutTracks{
        UI::UITimelineTrackDesc{.node = *styleNode,
                                .property = UI::UIAnimatableProperty::LayoutWidth,
                                .keyframes = widthFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .tracks = layoutTracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    ASSERT_TRUE(context
                    ->beginBackgroundColorTransition(
                        *directNode, UI::rgba8(200, 0, 0, 255),
                        UI::UITransitionSpec{
                            .property = UI::UIAnimatableProperty::BackgroundColor,
                            .duration = Core::Duration{0.100},
                            .easing = UI::UIEasing::Linear,
                        })
                    .has_value());
    ASSERT_TRUE(updater.setEnabled(*styleNode, false).has_value());
    ASSERT_TRUE(updater
                    .setBoxPaint(root.rootNodeId(),
                                 UI::makeSolidBox(UI::rgba8(1, 2, 3, 255)))
                    .has_value());

    clock.advance(Core::Duration{0.050});
    const Core::Status rejected = context->commitLayout({.width = 100.0F, .height = 60.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_EQ(context->statistics().motion.reservedTrackCount, 1U);
    const auto* rejectedStyle = findSolidPaint(context->committedPaint(), *styleNode);
    const auto* rejectedDirect = findSolidPaint(context->committedPaint(), *directNode);
    ASSERT_NE(rejectedStyle, nullptr);
    ASSERT_NE(rejectedDirect, nullptr);
    EXPECT_EQ(rejectedStyle->solidFill, UI::premultiply(UI::rgba8(0, 0, 0, 255)));
    EXPECT_EQ(rejectedDirect->solidFill, UI::premultiply(UI::rgba8(0, 0, 0, 255)));

    ASSERT_TRUE(updater.setBoxPaint(root.rootNodeId(), {}).has_value());
    clock.advance(Core::Duration{0.025});
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    const auto* recoveredLayout = findLayoutEntry(context->committedLayout(), *styleNode);
    const auto* recoveredStyle = findSolidPaint(context->committedPaint(), *styleNode);
    const auto* recoveredDirect = findSolidPaint(context->committedPaint(), *directNode);
    ASSERT_NE(recoveredLayout, nullptr);
    ASSERT_NE(recoveredStyle, nullptr);
    ASSERT_NE(recoveredDirect, nullptr);
    EXPECT_FLOAT_EQ(recoveredLayout->worldRect.width, 50.0F);
    EXPECT_EQ(recoveredStyle->solidFill, UI::premultiply(UI::rgba8(0, 0, 0, 255)));
    EXPECT_EQ(recoveredDirect->solidFill, UI::premultiply(UI::rgba8(150, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 2U);

    clock.advance(Core::Duration{0.050});
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    const auto* finalLayout = findLayoutEntry(context->committedLayout(), *styleNode);
    const auto* styleMiddle = findSolidPaint(context->committedPaint(), *styleNode);
    const auto* directFinal = findSolidPaint(context->committedPaint(), *directNode);
    ASSERT_NE(finalLayout, nullptr);
    ASSERT_NE(styleMiddle, nullptr);
    ASSERT_NE(directFinal, nullptr);
    EXPECT_FLOAT_EQ(finalLayout->worldRect.width, 60.0F);
    EXPECT_EQ(styleMiddle->solidFill, UI::premultiply(UI::rgba8(50, 0, 0, 255)));
    EXPECT_EQ(directFinal->solidFill, UI::premultiply(UI::rgba8(200, 0, 0, 255)));
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
}

TEST(UIMotionTests, ReducedMotionSnapsActiveLayoutTimelineThroughOneAtomicCommit)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 4,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 3,
        .timelineKeyframeCapacity = 6,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel =
        UI::makePanelElement(overlayStyle(2.0F, 4.0F, 20.0F, 10.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(20, 30, 40, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(updater.setPointerHitPolicy(*node, UI::UIPointerHitPolicy::Targetable).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());

    const std::array widthFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(20.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(70.0F)},
    };
    const std::array heightFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(10.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(35.0F)},
    };
    const std::array offsetFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Offset(2.0F, 4.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Offset(22.0F, 14.0F)},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutWidth,
                                .keyframes = widthFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutHeight,
                                .keyframes = heightFrames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::LayoutOffset,
                                .keyframes = offsetFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{1.0},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    clock.advance(Core::Duration{0.5});
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    const UI::UILogicalRect middle{
        .x = 12.0F,
        .y = 9.0F,
        .width = 45.0F,
        .height = 22.5F,
    };
    ASSERT_NE(findLayoutEntry(context->committedLayout(), *node), nullptr);
    ASSERT_NE(findHitEntry(context->committedHit(), *node), nullptr);
    ASSERT_NE(findSolidPaint(context->committedPaint(), *node), nullptr);
    EXPECT_EQ(findLayoutEntry(context->committedLayout(), *node)->worldRect, middle);
    EXPECT_EQ(findHitEntry(context->committedHit(), *node)->worldRect, middle);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->worldRect, middle);
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);

    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 hitRevision = context->committedHit().hitRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();
    ASSERT_TRUE(context->setReducedMotion(true).has_value());
    EXPECT_TRUE(context->reducedMotion());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    EXPECT_EQ(findLayoutEntry(context->committedLayout(), *node)->worldRect, middle);
    EXPECT_EQ(findHitEntry(context->committedHit(), *node)->worldRect, middle);
    EXPECT_EQ(findSolidPaint(context->committedPaint(), *node)->worldRect, middle);

    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    const auto* finalLayout = findLayoutEntry(context->committedLayout(), *node);
    const auto* finalHit = findHitEntry(context->committedHit(), *node);
    const auto* finalPaint = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(finalLayout, nullptr);
    ASSERT_NE(finalHit, nullptr);
    ASSERT_NE(finalPaint, nullptr);
    const UI::UILogicalRect final{
        .x = 22.0F,
        .y = 14.0F,
        .width = 70.0F,
        .height = 35.0F,
    };
    EXPECT_EQ(finalLayout->worldRect, final);
    EXPECT_EQ(finalHit->worldRect, final);
    EXPECT_EQ(finalPaint->worldRect, final);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision + 1U);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision + 1U);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);

    const u64 finalLayoutRevision = context->committedLayout().layoutRevision();
    const u64 finalHitRevision = context->committedHit().hitRevision();
    const u64 finalPaintRevision = context->committedPaint().paintRevision();
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}).has_value());
    EXPECT_EQ(context->committedLayout().layoutRevision(), finalLayoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), finalHitRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), finalPaintRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);
}

TEST(UIMotionTests, TimelinePlayFailsClosedWhenPaintTargetsCannotReserveDirtyQueue)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 1,
        .paintSnapshotCapacity = 4,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 2,
        .timelineKeyframeCapacity = 4,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    const auto child = updater.createElement(root.rootNodeId(), UI::makePanelElement(fixedSize(20.0F, 10.0F)));
    ASSERT_TRUE(child.has_value()) << child.error().message;
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array rootFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 0))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(20, 30, 40, 255))},
    };
    const std::array childFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 0))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(50, 60, 70, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = root.rootNodeId(),
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = rootFrames},
        UI::UITimelineTrackDesc{.node = *child,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = childFrames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.100},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;

    const Core::Status play = context->playTimeline(*timeline);
    ASSERT_FALSE(play.has_value());
    EXPECT_EQ(play.error().code, UI::UIErrorCode::CapacityExceeded);
    ASSERT_TRUE(context->isTimelineActive(*timeline).has_value());
    EXPECT_FALSE(*context->isTimelineActive(*timeline));
    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.dirtyQueuePendingCount, 0U);
    EXPECT_FALSE(statistics.layoutDirty);
    EXPECT_FALSE(statistics.paintDirty);
    EXPECT_TRUE(context->committedPaint().empty());
}

TEST(UIMotionTests, TimelineActiveCapacityFailurePreservesExistingPlayback)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 16,
        .timelineCapacity = 2,
        .timelineTrackCapacity = 2,
        .timelineKeyframeCapacity = 4,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto firstNode = updater.createElement(root.rootNodeId(), panel);
    const auto secondNode = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array firstFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(80, 0, 0, 255))},
    };
    const std::array secondFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(8.0F)},
    };
    const std::array firstTracks{
        UI::UITimelineTrackDesc{.node = *firstNode,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = firstFrames},
    };
    const std::array secondTracks{
        UI::UITimelineTrackDesc{.node = *secondNode,
                                .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = secondFrames},
    };
    auto firstTimeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = firstTracks});
    auto secondTimeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = secondTracks});
    ASSERT_TRUE(firstTimeline.has_value());
    ASSERT_TRUE(secondTimeline.has_value());

    ASSERT_TRUE(context->playTimeline(*firstTimeline).has_value());
    const Core::Status secondPlay = context->playTimeline(*secondTimeline);
    ASSERT_FALSE(secondPlay.has_value());
    EXPECT_EQ(secondPlay.error().code, UI::UIErrorCode::CapacityExceeded);
    ASSERT_TRUE(context->isTimelineActive(*firstTimeline).has_value());
    EXPECT_TRUE(*context->isTimelineActive(*firstTimeline));
    ASSERT_TRUE(context->isTimelineActive(*secondTimeline).has_value());
    EXPECT_FALSE(*context->isTimelineActive(*secondTimeline));
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 1U);

    clock.advance(Core::Duration{0.1});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
}

TEST(UIMotionTests, TimelineReplaceCapacityFailurePreservesActiveDefinitionAtomically)
{
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 8,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 1,
        .timelineKeyframeCapacity = 2,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array originalFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(80, 0, 0, 255))},
    };
    const std::array originalTracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = originalFrames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = originalTracks});
    ASSERT_TRUE(timeline.has_value());
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());

    clock.advance(Core::Duration{0.025});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* beforeFailure = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(beforeFailure, nullptr);
    EXPECT_EQ(beforeFailure->solidFill.red, 20);
    const usize cancelCount = context->statistics().motion.timelineCancelCount;

    const std::array oversizedFrames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(10, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 0.5F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(200, 0, 0, 255))},
    };
    const std::array oversizedTracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = oversizedFrames},
    };
    const Core::Status replace = context->replaceTimeline(
        *timeline,
        UI::UITimelineDesc{.duration = Core::Duration{0.2}, .tracks = oversizedTracks});
    ASSERT_FALSE(replace.has_value());
    EXPECT_EQ(replace.error().code, UI::UIErrorCode::CapacityExceeded);
    ASSERT_TRUE(context->isTimelineActive(*timeline).has_value());
    EXPECT_TRUE(*context->isTimelineActive(*timeline));
    EXPECT_EQ(context->statistics().motion.timelineTrackCount, 1U);
    EXPECT_EQ(context->statistics().motion.keyframeCount, 2U);
    EXPECT_EQ(context->statistics().motion.timelineCancelCount, cancelCount);

    clock.advance(Core::Duration{0.075});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* completed = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(completed, nullptr);
    EXPECT_EQ(completed->solidFill.red, 80);
}

TEST(UIMotionTests, TimelineRetargetUsesCurrentPresentationAsEffectiveFirstKeyframe)
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
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 0.5F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(200, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value());
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());

    clock.advance(Core::Duration{0.025});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* firstQuarter = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(firstQuarter, nullptr);
    EXPECT_EQ(firstQuarter->solidFill.red, 50);

    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    EXPECT_EQ(context->statistics().motion.timelineRetargetCount, 1U);
    clock.advance(Core::Duration{0.025});
    ASSERT_TRUE(context->sampleMotion(clock.now()).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* retargetedQuarter = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(retargetedQuarter, nullptr);
    EXPECT_EQ(retargetedQuarter->solidFill.red, 75);
}

TEST(UIMotionTests, TimelineIdentityRejectsStaleGenerationAndForeignContext)
{
    auto firstContext = createMotionContext();
    auto secondContext = createMotionContext();
    ASSERT_NE(firstContext, nullptr);
    ASSERT_NE(secondContext, nullptr);

    auto firstRootResult = firstContext->rootBuilder().createRoot();
    auto secondRootResult = secondContext->rootBuilder().createRoot();
    ASSERT_TRUE(firstRootResult.has_value());
    ASSERT_TRUE(secondRootResult.has_value());
    UI::UIRootOwner firstRoot = std::move(*firstRootResult);
    UI::UIRootOwner secondRoot = std::move(*secondRootResult);
    auto firstUpdaterResult = firstContext->treeUpdater(firstRoot);
    auto secondUpdaterResult = secondContext->treeUpdater(secondRoot);
    ASSERT_TRUE(firstUpdaterResult.has_value());
    ASSERT_TRUE(secondUpdaterResult.has_value());
    UI::UITreeUpdater firstUpdater = std::move(*firstUpdaterResult);
    UI::UITreeUpdater secondUpdater = std::move(*secondUpdaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto firstNode = firstUpdater.createElement(firstRoot.rootNodeId(), panel);
    const auto secondNode = secondUpdater.createElement(secondRoot.rootNodeId(), panel);
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(4.0F)},
    };
    const std::array firstTracks{
        UI::UITimelineTrackDesc{.node = *firstNode,
                                .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = frames},
    };
    const std::array secondTracks{
        UI::UITimelineTrackDesc{.node = *secondNode,
                                .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = frames},
    };
    auto stale = firstContext->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = firstTracks});
    auto foreignPeer = secondContext->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = secondTracks});
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE(foreignPeer.has_value());
    EXPECT_EQ(stale->index(), foreignPeer->index());
    EXPECT_EQ(stale->generation(), foreignPeer->generation());
    EXPECT_NE(*stale, *foreignPeer);
    EXPECT_FALSE(secondContext->isTimelineActive(*stale).has_value());
    EXPECT_FALSE(secondContext->playTimeline(*stale).has_value());

    ASSERT_TRUE(firstContext->destroyTimeline(*stale).has_value());
    auto replacement = firstContext->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = firstTracks});
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(stale->index(), replacement->index());
    EXPECT_NE(stale->generation(), replacement->generation());
    EXPECT_FALSE(firstContext->isTimelineActive(*stale).has_value());
    EXPECT_FALSE(firstContext->playTimeline(*stale).has_value());
    ASSERT_TRUE(firstContext->playTimeline(*replacement).has_value());
}

TEST(UIMotionTests, InactiveTimelineCancelAndDestroyDoNotApplyAuthoredTarget)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(10, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(10, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(200, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value());

    ASSERT_TRUE(context->cancelTimeline(*timeline).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* afterCancel = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(afterCancel, nullptr);
    EXPECT_EQ(afterCancel->solidFill.red, 10);

    ASSERT_TRUE(context->destroyTimeline(*timeline).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* afterDestroy = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(afterDestroy, nullptr);
    EXPECT_EQ(afterDestroy->solidFill.red, 10);
    EXPECT_FALSE(context->isTimelineActive(*timeline).has_value());
}

TEST(UIMotionTests, TimelineActivityQueryRejectsWrongOwnerThread)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    const auto node = updater.createElement(
        root.rootNodeId(), UI::makePanelElement(fixedSize(20.0F, 20.0F)));
    ASSERT_TRUE(node.has_value());
    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Scalar(0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Scalar(4.0F)},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value());

    bool queryHasValue = true;
    Core::ErrorCode queryError{};
    std::thread worker([&] {
        const Core::Result<bool> query = context->isTimelineActive(*timeline);
        queryHasValue = query.has_value();
        if (!query)
        {
            queryError = query.error().code;
        }
    });
    worker.join();

    EXPECT_FALSE(queryHasValue);
    EXPECT_EQ(queryError, UI::UIErrorCode::WrongOwnerThread);
}

TEST(UIMotionTests, TimelineWarmPlaybackDoesNotAllocateFromContextResource)
{
    CountingMemoryResource resource;
    UI::UIContextCapacityConfig config{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 8,
        .timelineCapacity = 1,
        .timelineTrackCapacity = 1,
        .timelineKeyframeCapacity = 3,
        .activeTimelineCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    auto contextResult = UI::UIContext::Create(makeMotionWindow(), config, resource);
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    FakeMotionClock clock;
    ASSERT_TRUE(context->setMotionClock(&clock).has_value());
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 0.5F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(50, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node,
                                .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(
        UI::UITimelineDesc{.duration = Core::Duration{0.1}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value());

    const auto runPlayback = [&] {
        EXPECT_TRUE(context->playTimeline(*timeline).has_value());
        clock.advance(Core::Duration{0.05});
        EXPECT_TRUE(context->sampleMotion(clock.now()).has_value());
        clock.advance(Core::Duration{0.05});
        EXPECT_TRUE(context->sampleMotion(clock.now()).has_value());
        EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    };
    runPlayback();
    const usize allocationCallsAfterWarmup = resource.allocationCalls();
    runPlayback();
    runPlayback();
    EXPECT_EQ(resource.allocationCalls(), allocationCallsAfterWarmup);
}

TEST(UIMotionTests, TimelineValidationConflictAndReducedMotionAreFailClosed)
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
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());

    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Color(UI::rgba8(80, 0, 0, 255))},
    };
    const std::array duplicateTracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
    };
    const auto invalid = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.1}, .tracks = duplicateTracks});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(context->statistics().motion.timelineCount, 0U);

    const std::array validTracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::BackgroundColor,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.1}, .tracks = validTracks});
    ASSERT_TRUE(timeline.has_value());
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    UI::UITransitionSpec direct{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.1},
    };
    EXPECT_FALSE(context->beginBackgroundColorTransition(*node, UI::rgba8(0, 80, 0, 255), direct));

    ASSERT_TRUE(context->setReducedMotion(true).has_value());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    EXPECT_TRUE(context->isTimelineActive(*timeline).has_value());
    EXPECT_FALSE(*context->isTimelineActive(*timeline));
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const auto* snapped = findSolidPaint(context->committedPaint(), *node);
    ASSERT_NE(snapped, nullptr);
    EXPECT_EQ(snapped->solidFill.red, 80);
}

TEST(UIMotionTests, DestroyingBoundNodeCancelsTimelineAndStaleDefinitionFails)
{
    auto context = createMotionContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UIElementDescriptor panel = UI::makePanelElement(fixedSize(20.0F, 20.0F));
    const auto node = updater.createElement(root.rootNodeId(), panel);
    ASSERT_TRUE(node.has_value());
    ASSERT_TRUE(context->commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    const std::array frames{
        UI::UIKeyframe{.normalizedTime = 0.0F, .value = UI::UIKeyframeValue::Scalar(0.0F)},
        UI::UIKeyframe{.normalizedTime = 1.0F, .value = UI::UIKeyframeValue::Scalar(4.0F)},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{.node = *node, .property = UI::UIAnimatableProperty::CornerRadius,
                                .keyframes = frames},
    };
    auto timeline = context->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.1}, .tracks = tracks});
    ASSERT_TRUE(timeline.has_value());
    ASSERT_TRUE(context->playTimeline(*timeline).has_value());
    ASSERT_TRUE(updater.destroy(*node).has_value());
    EXPECT_EQ(context->statistics().motion.activeTimelineCount, 0U);
    ASSERT_TRUE(context->destroyTimeline(*timeline).has_value());
    EXPECT_FALSE(context->cancelTimeline(*timeline));
}

} // namespace
} // namespace Tina::Tests
