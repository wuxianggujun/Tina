#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/ui/UI.hpp>

#include "../../../src/runtime/input/ActionMapper.hpp"
#include "../../../src/runtime/input/UIInputRouteProducer.hpp"

#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using Runtime::Input::ActionMapper;
using Runtime::Input::UIInputRouteProducer;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

inline constexpr InputActionId PointerAction{41};

struct FrameSpec final {
    Platform::PlatformFrameId frameId{1};
    std::vector<Platform::InputTransitionPayload> transitions{};
    std::vector<Platform::Key> heldKeys{};
    std::vector<Platform::PointerButton> heldPointerButtons{};
    double pointerX = 10.0;
    double pointerY = 10.0;
    double accumulatedDeltaX = 0.0;
    double accumulatedDeltaY = 0.0;
};

struct RouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId target{};
};

struct PointerEventTrace final {
    std::array<UI::UIPointerInputEvent, 8> events{};
    usize size = 0;

    void push(const UI::UIPointerInputEvent& event) noexcept
    {
        if (size < events.size())
        {
            events[size] = event;
            ++size;
        }
    }
};

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

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] Platform::PointerMoveTransition pointerMove(Platform::WindowId window, double x, double y,
                                                          double deltaX = 0.0, double deltaY = 0.0) noexcept
{
    return Platform::PointerMoveTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .logicalX = x,
        .logicalY = y,
        .deltaX = deltaX,
        .deltaY = deltaY,
    };
}

[[nodiscard]] Platform::PointerButtonTransition
pointerButton(Platform::WindowId window, Platform::DigitalTransition state, double x, double y) noexcept
{
    return Platform::PointerButtonTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .button = Platform::PointerButton::Primary,
        .state = state,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Platform::PointerWheelTransition pointerWheel(Platform::WindowId window, double x, double y,
                                                            double deltaX, double deltaY) noexcept
{
    return Platform::PointerWheelTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .deltaX = deltaX,
        .deltaY = deltaY,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Platform::KeyTransition keyDown(Platform::WindowId window, Platform::Key key) noexcept
{
    return Platform::KeyTransition{
        .window = window,
        .key = key,
        .state = Platform::DigitalTransition::Down,
    };
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIRoutedPointerListenerToken
addListener(UI::UIContext& context, UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback)
{
    auto result = context.addRoutedPointerListener(descriptor, std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

[[nodiscard]] RouteTree createRouteTree(Platform::WindowId window,
                                        UI::UIContextCapacityConfig capacities =
                                            {
                                                .nodeCapacity = 8,
                                                .rootCapacity = 1,
                                                .routePathCapacity = 8,
                                                .routedPointerListenerCapacity = 16,
                                            },
                                        std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    RouteTree tree;
    auto context = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);

    auto root = tree.context->rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto panel = tree.context->rootBuilder().createPanel(tree.root.rootNodeId());
    EXPECT_TRUE(panel.has_value()) << (panel ? "" : panel.error().message);
    if (!panel)
    {
        return tree;
    }
    auto target = tree.context->rootBuilder().createButton(*panel);
    EXPECT_TRUE(target.has_value()) << (target ? "" : target.error().message);
    if (!target)
    {
        return tree;
    }
    tree.panel = *panel;
    tree.target = *target;

    auto updater = tree.context->treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);
    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.panel, fixedSize(80.0F, 80.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setPointerHitPolicy(tree.target, UI::UIPointerHitPolicy::Targetable));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] Core::Result<Platform::PlatformFrameView> buildFrame(Platform::PlatformFrameBuilder& builder,
                                                                   Platform::WindowId window, const FrameSpec& spec)
{
    if (auto status = builder.beginFrame(spec.frameId); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    const Platform::WindowMetricsSnapshot metrics{
        .window = window,
        .logicalExtent = {100, 100},
        .framebufferExtent = {100, 100},
        .contentScale = {1.0F, 1.0F},
        .revision = spec.frameId.value,
        .focused = true,
        .visible = true,
    };
    Platform::WindowInputSnapshot input{
        .window = window,
        .sourceMetricsRevision = spec.frameId.value,
    };
    input.pointer.logicalX = spec.pointerX;
    input.pointer.logicalY = spec.pointerY;
    input.pointer.accumulatedDeltaX = spec.accumulatedDeltaX;
    input.pointer.accumulatedDeltaY = spec.accumulatedDeltaY;
    for (Platform::Key key : spec.heldKeys)
    {
        input.heldKeys.set(static_cast<usize>(key));
    }
    for (Platform::PointerButton button : spec.heldPointerButtons)
    {
        input.pointer.heldButtons.set(static_cast<usize>(button));
    }
    if (!builder.setPrimaryWindowSnapshot(metrics, input) || !builder.setGamepadSnapshots({}))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "test frame snapshot was rejected");
    }
    for (const Platform::InputTransitionPayload& transition : spec.transitions)
    {
        const Platform::FrameBatchAppendResult result = builder.appendInputTransition(transition);
        if (result != Platform::FrameBatchAppendResult::Appended &&
            result != Platform::FrameBatchAppendResult::Coalesced &&
            result != Platform::FrameBatchAppendResult::ResetInserted)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test transition append failed");
        }
    }
    return builder.finishFrame();
}

[[nodiscard]] std::unique_ptr<UIInputRouteProducer>
createProducer(usize rawTransitionCapacity = 128,
               std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto producer = UIInputRouteProducer::Create(rawTransitionCapacity, resource);
    EXPECT_TRUE(producer.has_value()) << (producer ? "" : producer.error().message);
    return producer ? std::move(*producer) : nullptr;
}

[[nodiscard]] std::unique_ptr<ActionMapper> createPointerMapper()
{
    const std::array bindings{
        DigitalActionBinding{
            .input =
                PrimaryPointerButtonBinding{
                    .pointer = Platform::PrimaryPointerId,
                    .button = Platform::PointerButton::Primary,
                },
            .action = PointerAction,
            .domain = InputActionDomain::Simulation,
        },
    };
    auto mapper = ActionMapper::Create(bindings);
    EXPECT_TRUE(mapper.has_value()) << (mapper ? "" : mapper.error().message);
    return mapper ? std::move(*mapper) : nullptr;
}

[[nodiscard]] const DigitalActionTransition* digital(const SimulationActionTransition& transition)
{
    return std::get_if<DigitalActionTransition>(&transition);
}

class UIInputRouteProducerTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(2);
        ASSERT_TRUE(poolResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*poolResult));
        auto first = windows->tryEmplace(1);
        auto second = windows->tryEmplace(2);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        window = *first;
        otherWindow = *second;
        auto builderResult = Platform::PlatformFrameBuilder::Create({
            .inputTransitionCapacity = 128,
            .platformEventCapacity = 1,
        });
        ASSERT_TRUE(builderResult.has_value()) << (builderResult ? "" : builderResult.error().message);
        builder = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*builderResult));
    }

    std::unique_ptr<WindowPool> windows;
    std::unique_ptr<Platform::PlatformFrameBuilder> builder;
    Platform::WindowId window{};
    Platform::WindowId otherWindow{};
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

TEST_F(UIInputRouteProducerTest, MapsMixedRawOrdinalsWithHolesToFrameAndSequence)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(token);

    FrameSpec spec{
        .frameId = {7},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                pointerMove(window, 90.0, 90.0),
                keyDown(window, Platform::Key::B),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
            },
        .heldKeys = {Platform::Key::A, Platform::Key::B},
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 1U);
    EXPECT_EQ(observed.events[0].platformFrame, frame->id());
    EXPECT_EQ(observed.events[0].transitionOrdinal, 3U);
    EXPECT_EQ(observed.events[0].sourceSequence, frame->inputTransitions()[3].sequence);
    EXPECT_TRUE(output->consumption.isConsumed(3));
    EXPECT_FALSE(output->consumption.isConsumed(0));
    EXPECT_FALSE(output->consumption.isConsumed(1));
    EXPECT_FALSE(output->consumption.isConsumed(2));
}

TEST_F(UIInputRouteProducerTest, PreservesPointerTransitionPositions)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto buttonToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    auto wheelToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(buttonToken && wheelToken);

    FrameSpec spec{
        .frameId = {8},
        .transitions =
            {
                pointerMove(window, 10.0, 10.0, 1.0, 1.0),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                pointerMove(window, 30.0, 30.0, 20.0, 20.0),
                pointerWheel(window, 30.0, 30.0, 0.0, -1.0),
                pointerMove(window, 50.0, 50.0, 20.0, 20.0),
            },
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 50.0,
        .pointerY = 50.0,
        .accumulatedDeltaX = 41.0,
        .accumulatedDeltaY = 41.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 2U);
    EXPECT_EQ(observed.events[0].kind, UI::UIRoutedPointerEventKind::ButtonDown);
    EXPECT_FLOAT_EQ(observed.events[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(observed.events[0].position.y, 10.0F);
    EXPECT_EQ(observed.events[1].kind, UI::UIRoutedPointerEventKind::Wheel);
    EXPECT_FLOAT_EQ(observed.events[1].position.x, 30.0F);
    EXPECT_FLOAT_EQ(observed.events[1].position.y, 30.0F);
    EXPECT_TRUE(output->consumption.isConsumed(1));
    EXPECT_TRUE(output->consumption.isConsumed(3));
}

TEST_F(UIInputRouteProducerTest, ConsumedBitsCoverOrdinalsSixtyThreeAndSixtyFourAndClearNextFrame)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto consumeButton = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    auto consumeWheel = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    ASSERT_TRUE(consumeButton && consumeWheel);

    FrameSpec first{.frameId = {63}};
    for (usize index = 0; index < 63; ++index)
    {
        first.transitions.push_back(keyDown(window, Platform::Key::A));
    }
    first.transitions.push_back(pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0));
    first.transitions.push_back(pointerWheel(window, 10.0, 10.0, 0.0, 1.0));
    first.heldKeys = {Platform::Key::A};
    first.heldPointerButtons = {Platform::PointerButton::Primary};
    first.pointerX = 10.0;
    first.pointerY = 10.0;
    auto firstFrame = buildFrame(*builder, window, first);
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);

    auto firstOutput = producer->produce(tree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(63));
    EXPECT_TRUE(firstOutput->consumption.isConsumed(64));
    ASSERT_EQ(firstOutput->consumption.consumedOrdinalWords.size(), 2U);

    FrameSpec second{
        .frameId = {64},
        .transitions = {pointerMove(window, 10.0, 10.0)},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto secondFrame = buildFrame(*builder, window, second);
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    auto secondOutput = producer->produce(nullptr, *secondFrame);
    ASSERT_TRUE(secondOutput.has_value()) << (secondOutput ? "" : secondOutput.error().message);
    EXPECT_FALSE(secondOutput->consumption.isConsumed(0));
    EXPECT_TRUE(secondOutput->consumption.consumedOrdinalWords.empty());
}

TEST_F(UIInputRouteProducerTest, NoHitAndStoppedEventsDoNotConsume)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent& event) noexcept {
            ++callbackCount;
            event.stopPropagation();
        }});
    ASSERT_TRUE(token);

    auto noHitFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {9},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 90.0, 90.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 90.0,
                       .pointerY = 90.0,
                   });
    ASSERT_TRUE(noHitFrame.has_value()) << (noHitFrame ? "" : noHitFrame.error().message);
    auto noHitOutput = producer->produce(tree.context.get(), *noHitFrame);
    ASSERT_TRUE(noHitOutput.has_value()) << (noHitOutput ? "" : noHitOutput.error().message);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_FALSE(noHitOutput->consumption.isConsumed(0));

    auto stoppedFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {10},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(stoppedFrame.has_value()) << (stoppedFrame ? "" : stoppedFrame.error().message);
    auto stoppedOutput = producer->produce(tree.context.get(), *stoppedFrame);
    ASSERT_TRUE(stoppedOutput.has_value()) << (stoppedOutput ? "" : stoppedOutput.error().message);
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_FALSE(stoppedOutput->consumption.isConsumed(0));
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

TEST_F(UIInputRouteProducerTest, ResetCancelAndNonPointerTransitionsDoNotRouteOrFabricateUp)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto upToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonUp, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(upToken);

    FrameSpec spec{
        .frameId = {12},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                Platform::InputCancelTransition{
                    .routedWindow = window,
                    .reason = Platform::InputCancelReason::FocusLost,
                },
                Platform::InputStreamReset{
                    .routedWindow = window,
                    .reason = Platform::InputResetReason::BackendRecovery,
                },
            },
        .heldKeys = {Platform::Key::A},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(callbackCount, 0U);
    for (usize ordinal = 0; ordinal < frame->inputTransitions().size(); ++ordinal)
    {
        EXPECT_FALSE(output->consumption.isConsumed(ordinal));
    }
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
    expectOk(failingTree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

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
    auto producer = createProducer(128, resource);
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
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_TRUE(token);
    const usize allocationCountBeforeFrames = resource.allocationCount();
    ASSERT_GT(allocationCountBeforeFrames, 0U);

    for (u64 frameIndex = 1; frameIndex <= 300; ++frameIndex)
    {
        auto frame = buildFrame(*builder, window,
                                {
                                    .frameId = {frameIndex},
                                    .transitions = {pointerMove(window, 10.0, 10.0)},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
        ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
        auto output = producer->produce(tree.context.get(), *frame);
        ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
        EXPECT_EQ(output->consumption.platformFrame, frame->id());
        EXPECT_TRUE(output->claims.controls.empty());
    }
    EXPECT_EQ(resource.allocationCount(), allocationCountBeforeFrames);
}

TEST_F(UIInputRouteProducerTest, CapacityValidationAllowsOneReservedResetSlot)
{
    EXPECT_FALSE(UIInputRouteProducer::Create(0).has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     static_cast<usize>(Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity) + 1U)
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

TEST_F(UIInputRouteProducerTest, ActionMapperSuppressesConsumedDownUntilTrueUpThenRestoresUnconsumedDown)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto consumeToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    ASSERT_TRUE(consumeToken);

    auto consumedDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {21},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(consumedDown.has_value()) << (consumedDown ? "" : consumedDown.error().message);
    auto consumedOutput = producer->produce(tree.context.get(), *consumedDown);
    ASSERT_TRUE(consumedOutput.has_value()) << (consumedOutput ? "" : consumedOutput.error().message);
    ASSERT_TRUE(mapper->mapFrame(*consumedDown, consumedOutput->consumption, consumedOutput->claims, 0, 0).has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isHeld(PointerAction));

    auto stillHeld = buildFrame(*builder, window,
                                {
                                    .frameId = {22},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
    ASSERT_TRUE(stillHeld.has_value()) << (stillHeld ? "" : stillHeld.error().message);
    auto stillHeldOutput = producer->produce(nullptr, *stillHeld);
    ASSERT_TRUE(stillHeldOutput.has_value()) << (stillHeldOutput ? "" : stillHeldOutput.error().message);
    ASSERT_TRUE(mapper->mapFrame(*stillHeld, stillHeldOutput->consumption, stillHeldOutput->claims, 1, 0).has_value());
    auto stillSuppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(stillSuppressed.has_value()) << (stillSuppressed ? "" : stillSuppressed.error().message);
    EXPECT_TRUE(stillSuppressed->transitions.empty());

    auto trueUp = buildFrame(*builder, window,
                             {
                                 .frameId = {23},
                                 .transitions = {pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                                 .pointerX = 10.0,
                                 .pointerY = 10.0,
                             });
    ASSERT_TRUE(trueUp.has_value()) << (trueUp ? "" : trueUp.error().message);
    auto upOutput = producer->produce(nullptr, *trueUp);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    ASSERT_TRUE(mapper->mapFrame(*trueUp, upOutput->consumption, upOutput->claims, 2, 0).has_value());
    auto afterUp = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(afterUp.has_value()) << (afterUp ? "" : afterUp.error().message);
    EXPECT_TRUE(afterUp->transitions.empty());

    auto downAgain =
        buildFrame(*builder, window,
                   {
                       .frameId = {24},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(downAgain.has_value()) << (downAgain ? "" : downAgain.error().message);
    auto downAgainOutput = producer->produce(nullptr, *downAgain);
    ASSERT_TRUE(downAgainOutput.has_value()) << (downAgainOutput ? "" : downAgainOutput.error().message);
    ASSERT_TRUE(mapper->mapFrame(*downAgain, downAgainOutput->consumption, downAgainOutput->claims, 3, 0).has_value());
    auto restored = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(restored.has_value()) << (restored ? "" : restored.error().message);
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(restored->isHeld(PointerAction));
}

} // namespace
} // namespace Tina::Tests
