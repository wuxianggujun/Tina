#include "UIInputRouteProducerTestSupport.hpp"

#include <limits>
#include <memory_resource>
#include <variant>

namespace Tina::Tests {
namespace {

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCount_;
        return upstream_->allocate(bytes, alignment);
    }

    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override
    {
        upstream_->deallocate(memory, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
    usize allocationCount_ = 0;
};

TEST_F(UIInputRouteProducerTest, NullContextProducesNoneViews)
{
    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {1},
                                .transitions = {pointerMove(window, 10.0, 10.0)},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(nullptr, *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(output->consumption.platformFrame, frame->id());
    EXPECT_EQ(output->consumption.transitionCount, frame->inputTransitions().size());
    EXPECT_TRUE(output->consumption.consumedOrdinalWords.empty());
    EXPECT_EQ(output->claims.platformFrame, frame->id());
    EXPECT_TRUE(output->claims.controls.empty());
}
TEST_F(UIInputRouteProducerTest, OwnerMismatchFailsBeforeAnyCallback)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(otherWindow);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(token);

    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {11},
                                .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                                .heldPointerButtons = {Platform::PointerButton::Primary},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(callbackCount, 0U);
}
TEST_F(UIInputRouteProducerTest, RouteFailureDoesNotPublishOrReplayEarlierListenerSideEffects)
{
    auto producer = createProducer();
    RouteTree goodTree = createRouteTree(window);
    RouteTree failingTree = createRouteTree(window, {
                                                        .nodeCapacity = 8,
                                                        .rootCapacity = 1,
                                                        .routePathCapacity = 1,
                                                        .routedPointerListenerCapacity = 16,
                                                    });
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(goodTree.context, nullptr);
    ASSERT_NE(failingTree.context, nullptr);
    expectOk(
        failingTree.updater.setPointerHitPolicy(failingTree.root.rootNodeId(), UI::UIPointerHitPolicy::Targetable));
    expectOk(failingTree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto consumeToken = addListener(
        *goodTree.context,
        {.node = goodTree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    usize failingCallbackCount = 0;
    auto failingToken =
        addListener(*failingTree.context,
                    {.node = failingTree.root.rootNodeId(),
                     .kind = UI::UIRoutedPointerEventKind::Move,
                     .phases = UI::UIEventPhaseMask::Target},
                    UI::UIRoutedPointerCallback{
                        [&failingCallbackCount](UI::UIRoutedPointerEvent&) noexcept { ++failingCallbackCount; }});
    ASSERT_TRUE(consumeToken && failingToken);

    auto firstFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {13},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstOutput = producer->produce(goodTree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(0));

    auto failingFrame = buildFrame(*builder, window,
                                   {
                                       .frameId = {14},
                                       .transitions =
                                           {
                                               pointerMove(window, 90.0, 90.0),
                                               pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                                           },
                                       .heldPointerButtons = {Platform::PointerButton::Primary},
                                       .pointerX = 10.0,
                                       .pointerY = 10.0,
                                   });
    ASSERT_TRUE(failingFrame.has_value()) << (failingFrame ? "" : failingFrame.error().message);
    auto failedOutput = producer->produce(failingTree.context.get(), *failingFrame);
    EXPECT_FALSE(failedOutput.has_value());
    EXPECT_EQ(failingCallbackCount, 1U);
    EXPECT_EQ(firstOutput->consumption.platformFrame, Platform::PlatformFrameId{13});
    EXPECT_TRUE(firstOutput->consumption.isConsumed(0));

    auto sameFrameRetry = producer->produce(failingTree.context.get(), *failingFrame);
    EXPECT_FALSE(sameFrameRetry.has_value());
    EXPECT_EQ(failingCallbackCount, 1U);

    auto cleanFrame = buildFrame(*builder, window,
                                 {
                                     .frameId = {15},
                                     .transitions = {pointerMove(window, 90.0, 90.0)},
                                     .pointerX = 90.0,
                                     .pointerY = 90.0,
                                 });
    ASSERT_TRUE(cleanFrame.has_value()) << (cleanFrame ? "" : cleanFrame.error().message);
    auto cleanOutput = producer->produce(nullptr, *cleanFrame);
    ASSERT_TRUE(cleanOutput.has_value()) << (cleanOutput ? "" : cleanOutput.error().message);
    EXPECT_FALSE(cleanOutput->consumption.isConsumed(0));
}
TEST_F(UIInputRouteProducerTest, ThreeHundredFramesPerformNoObservedPmrAllocations)
{
    ObservingMemoryResource resource;
    auto producer = createProducer(
        128, InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity, resource);
    RouteTree tree = createRouteTree(window,
                                     {
                                         .nodeCapacity = 8,
                                         .rootCapacity = 1,
                                         .routePathCapacity = 8,
                                         .routedPointerListenerCapacity = 16,
                                     },
                                     resource);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            (void)event.claimPointerButton(Platform::PointerButton::Primary);
        }});
    ASSERT_TRUE(token);
    const usize allocationCountBeforeFrames = resource.allocationCount();
    ASSERT_GT(allocationCountBeforeFrames, 0U);

    for (u64 frameIndex = 1; frameIndex <= 300; ++frameIndex)
    {
        auto frame = buildFrame(*builder, window,
                                {
                                    .frameId = {frameIndex},
                                    .transitions = {pointerMove(window, 10.0, 10.0)},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
        ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
        auto output = producer->produce(tree.context.get(), *frame);
        ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
        EXPECT_EQ(output->consumption.platformFrame, frame->id());
        EXPECT_EQ(output->claims.controls.size(), 1U);
    }
    EXPECT_EQ(resource.allocationCount(), allocationCountBeforeFrames);
}
TEST_F(UIInputRouteProducerTest, CapacityValidationAllowsOneReservedResetSlot)
{
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     0, InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity)
                     .has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     static_cast<usize>(Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity) + 1U,
                     InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity)
                     .has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(1, 0).has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     1, InputActionMapperCapacityConfig::MaximumContinuousControlClaimCapacity + 1U)
                     .has_value());

    auto smallBuilder = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(smallBuilder.has_value()) << (smallBuilder ? "" : smallBuilder.error().message);
    auto producer = createProducer(1);
    ASSERT_NE(producer, nullptr);

    auto frame = buildFrame(*smallBuilder, window,
                            {
                                .frameId = {16},
                                .transitions =
                                    {
                                        keyDown(window, Platform::Key::A),
                                        Platform::InputStreamReset{
                                            .routedWindow = window,
                                            .reason = Platform::InputResetReason::BackendRecovery,
                                        },
                                    },
                                .heldKeys = {Platform::Key::A},
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    ASSERT_EQ(frame->inputTransitions().size(), 2U);
    EXPECT_NE(std::get_if<Platform::InputStreamReset>(&frame->inputTransitions()[1].payload), nullptr);

    auto output = producer->produce(nullptr, *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(output->consumption.transitionCount, 2U);
    EXPECT_TRUE(output->consumption.consumedOrdinalWords.empty());
    EXPECT_TRUE(output->claims.controls.empty());
}
TEST_F(UIInputRouteProducerTest, FloatUnrepresentablePointerValueFailsBeforeAnyCallback)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(token);

    const double unrepresentable = static_cast<double>((std::numeric_limits<float>::max)()) * 2.0;
    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {17},
                                .transitions =
                                    {
                                        pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                                        pointerWheel(window, unrepresentable, 10.0, 0.0, 1.0),
                                    },
                                .heldPointerButtons = {Platform::PointerButton::Primary},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(callbackCount, 0U);
}

} // namespace
} // namespace Tina::Tests
