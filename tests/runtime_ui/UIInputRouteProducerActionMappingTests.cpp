#include "UIInputRouteProducerTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(UIInputRouteProducerTest, ListViewNavigationConsumptionSuppressesGameplayActionUntilTrueRelease)
{
    auto producer = createProducer();
    auto mapper = createKeyMapper(Platform::Key::Down);
    CollectionRouteTree tree = createCollectionRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    expectOk(tree.context->input().requestFocus(tree.listView));

    auto down = buildFrame(
        *builder,
        window,
        {
            .frameId = {150},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*down, downOutput->consumption, downOutput->claims, 0, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(NavigationAction));
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    auto up = buildFrame(
        *builder,
        window,
        {
            .frameId = {151},
            .transitions = {keyUp(window, Platform::Key::Down)},
        });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*up, upOutput->consumption, upOutput->claims, 1, 1,
                               &lastPresentedCamera2D)
                    .has_value());
    auto released = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(released.has_value()) << (released ? "" : released.error().message);
    EXPECT_TRUE(released->transitions.empty());
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    expectOk(tree.context->input().clearFocus());
    auto gameplayDown = buildFrame(
        *builder,
        window,
        {
            .frameId = {152},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(gameplayDown.has_value()) << (gameplayDown ? "" : gameplayDown.error().message);
    auto gameplayOutput = producer->produce(tree.context.get(), *gameplayDown);
    ASSERT_TRUE(gameplayOutput.has_value()) << (gameplayOutput ? "" : gameplayOutput.error().message);
    EXPECT_FALSE(gameplayOutput->consumption.isConsumed(0));
    ASSERT_TRUE(mapper
                    ->mapFrame(*gameplayDown, gameplayOutput->consumption, gameplayOutput->claims, 2, 2,
                               &lastPresentedCamera2D)
                    .has_value());
    auto restored = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(restored.has_value()) << (restored ? "" : restored.error().message);
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(restored->isActive(NavigationAction));
}
TEST_F(UIInputRouteProducerTest, ButtonDefaultDownSuppressesGameplayUntilTrueUpThenRestoresUnconsumedDown)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);

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
    ASSERT_TRUE(mapper
                    ->mapFrame(*consumedDown, consumedOutput->consumption, consumedOutput->claims, 0, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(PointerAction));

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
    ASSERT_TRUE(mapper
                    ->mapFrame(*stillHeld, stillHeldOutput->consumption, stillHeldOutput->claims, 1, 0,
                               &lastPresentedCamera2D)
                    .has_value());
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
    ASSERT_TRUE(mapper
                    ->mapFrame(*trueUp, upOutput->consumption, upOutput->claims, 2, 0, &lastPresentedCamera2D)
                    .has_value());
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
    ASSERT_TRUE(mapper
                    ->mapFrame(*downAgain, downAgainOutput->consumption, downAgainOutput->claims, 3, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto restored = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(restored.has_value()) << (restored ? "" : restored.error().message);
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(restored->isActive(PointerAction));
}
TEST_F(UIInputRouteProducerTest, HeldPointerClaimCancelsObservedGameplayUntilTrueUp)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto claimToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(claimToken);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {31},
                               .transitions = {
                                   pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                               .heldPointerButtons = {Platform::PointerButton::Primary},
                               .pointerX = 10.0,
                               .pointerY = 10.0,
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(nullptr, *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*down, downOutput->consumption, downOutput->claims, 0, 0, &lastPresentedCamera2D)
                    .has_value());
    auto pressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    ASSERT_EQ(pressed->transitions.size(), 1U);
    ASSERT_NE(digital(pressed->transitions[0]), nullptr);
    EXPECT_EQ(digital(pressed->transitions[0])->kind, InputActionTransitionKind::Started);
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    auto claimed = buildFrame(*builder, window,
                              {
                                  .frameId = {32},
                                  .transitions = {pointerMove(window, 10.0, 10.0)},
                                  .heldPointerButtons = {Platform::PointerButton::Primary},
                                  .pointerX = 10.0,
                                  .pointerY = 10.0,
                              });
    ASSERT_TRUE(claimed.has_value()) << (claimed ? "" : claimed.error().message);
    auto claimedOutput = producer->produce(tree.context.get(), *claimed);
    ASSERT_TRUE(claimedOutput.has_value()) << (claimedOutput ? "" : claimedOutput.error().message);
    ASSERT_EQ(claimedOutput->claims.controls.size(), 1U);
    ASSERT_TRUE(mapper
                    ->mapFrame(*claimed, claimedOutput->consumption, claimedOutput->claims, 1, 1,
                               &lastPresentedCamera2D)
                    .has_value());
    auto cancelled = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(cancelled.has_value()) << (cancelled ? "" : cancelled.error().message);
    ASSERT_EQ(cancelled->transitions.size(), 1U);
    ASSERT_NE(digital(cancelled->transitions[0]), nullptr);
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, InputActionTransitionKind::Cancelled);
    EXPECT_FALSE(digital(cancelled->transitions[0])->worldPointerSample.has_value());
    EXPECT_FALSE(cancelled->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    auto up = buildFrame(*builder, window,
                         {
                             .frameId = {33},
                             .transitions = {
                                 pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                             .pointerX = 10.0,
                             .pointerY = 10.0,
                         });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(nullptr, *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*up, upOutput->consumption, upOutput->claims, 2, 2, &lastPresentedCamera2D)
                    .has_value());
    auto releasedSuppression = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(releasedSuppression.has_value())
        << (releasedSuppression ? "" : releasedSuppression.error().message);
    EXPECT_TRUE(releasedSuppression->transitions.empty());
    EXPECT_FALSE(releasedSuppression->isActive(PointerAction));
}
TEST_F(UIInputRouteProducerTest, PointerClaimInterceptsInitialDownWithoutTransitionConsumption)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto claimToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.preventDefaultAction();
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(claimToken);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {41},
                               .transitions = {
                                   pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                               .heldPointerButtons = {Platform::PointerButton::Primary},
                               .pointerX = 10.0,
                               .pointerY = 10.0,
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto output = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_FALSE(output->consumption.isConsumed(0));
    ASSERT_EQ(output->claims.controls.size(), 1U);

    ASSERT_TRUE(mapper
                    ->mapFrame(*down, output->consumption, output->claims, 0, 0, &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(PointerAction));
}

// Product-level M10-A39 gate: HUD Button default action (same surface as
// tina_sample_2d) must consume primary-pointer click so a world/gameplay Action
// bound to Primary does not fire; a miss must still map the world Action.
TEST_F(UIInputRouteProducerTest, ProductButtonClickDoesNotPenetrateWorldPointerAction)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    UI::UIButtonActivationSource lastSource = UI::UIButtonActivationSource::PrimaryPointer;
    u64 lastSourceSequence = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount, &lastSource, &lastSourceSequence](
                const UI::UIButtonActionEvent& event) noexcept {
                ++activationCount;
                lastSource = event.source;
                lastSourceSequence = event.sourceSequence;
            }}));

    // --- Hit path: Down + Up inside Button activates UI and never presses world Action ---
    auto hitDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {60},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(hitDown.has_value()) << (hitDown ? "" : hitDown.error().message);
    auto hitDownOutput = producer->produce(tree.context.get(), *hitDown);
    ASSERT_TRUE(hitDownOutput.has_value()) << (hitDownOutput ? "" : hitDownOutput.error().message);
    EXPECT_TRUE(hitDownOutput->consumption.isConsumed(0));
    auto pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_TRUE(*pressed);
    ASSERT_TRUE(mapper
                    ->mapFrame(*hitDown, hitDownOutput->consumption, hitDownOutput->claims, 0, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto suppressedDown = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressedDown.has_value()) << (suppressedDown ? "" : suppressedDown.error().message);
    EXPECT_TRUE(suppressedDown->transitions.empty());
    EXPECT_FALSE(suppressedDown->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    auto hitUp =
        buildFrame(*builder, window,
                   {
                       .frameId = {61},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(hitUp.has_value()) << (hitUp ? "" : hitUp.error().message);
    auto hitUpOutput = producer->produce(tree.context.get(), *hitUp);
    ASSERT_TRUE(hitUpOutput.has_value()) << (hitUpOutput ? "" : hitUpOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*hitUp, hitUpOutput->consumption, hitUpOutput->claims, 1, 1,
                               &lastPresentedCamera2D)
                    .has_value());
    auto afterUp = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(afterUp.has_value()) << (afterUp ? "" : afterUp.error().message);
    EXPECT_TRUE(afterUp->transitions.empty());
    EXPECT_FALSE(afterUp->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    EXPECT_EQ(activationCount, 1U);
    EXPECT_EQ(lastSource, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(lastSourceSequence, hitUp->inputTransitions()[0].sequence);
    auto released = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(released.has_value()) << (released ? "" : released.error().message);
    EXPECT_FALSE(*released);

    // --- Miss path: Down outside Button must map world Pointer Action once ---
    auto missDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {62},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Down, 90.0, 90.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 90.0,
                       .pointerY = 90.0,
                   });
    ASSERT_TRUE(missDown.has_value()) << (missDown ? "" : missDown.error().message);
    auto missDownOutput = producer->produce(tree.context.get(), *missDown);
    ASSERT_TRUE(missDownOutput.has_value()) << (missDownOutput ? "" : missDownOutput.error().message);
    EXPECT_FALSE(missDownOutput->consumption.isConsumed(0));
    ASSERT_TRUE(mapper
                    ->mapFrame(*missDown, missDownOutput->consumption, missDownOutput->claims, 2, 2,
                               &lastPresentedCamera2D)
                    .has_value());
    auto worldPressed = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(worldPressed.has_value()) << (worldPressed ? "" : worldPressed.error().message);
    ASSERT_EQ(worldPressed->transitions.size(), 1U);
    ASSERT_NE(digital(worldPressed->transitions[0]), nullptr);
    EXPECT_EQ(digital(worldPressed->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(worldPressed->isActive(PointerAction));
    EXPECT_EQ(activationCount, 1U);
}

} // namespace
} // namespace Tina::Tests
