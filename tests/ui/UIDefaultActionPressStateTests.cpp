#include <gtest/gtest.h>

#include "detail/UIDefaultActionPressState.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <optional>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
using PressState = UI::Detail::UIDefaultActionPressState;

class UIDefaultActionPressStateTests : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows_ = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto ownerWindowResult = windows_->tryEmplace(1);
        auto otherWindowResult = windows_->tryEmplace(2);
        ASSERT_TRUE(ownerWindowResult.has_value());
        ASSERT_TRUE(otherWindowResult.has_value());
        ownerWindow_ = *ownerWindowResult;
        otherWindow_ = *otherWindowResult;

        UI::UIContextCapacityConfig capacities{
            .nodeCapacity = 4,
            .rootCapacity = 1,
        };
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(ownerWindow_, capacities);
        ASSERT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        context_ = std::move(*contextResult);

        auto rootResult = context_->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value())
            << (rootResult ? "" : rootResult.error().message);
        root_ = std::move(*rootResult);
        auto firstNodeResult =
            context_->rootBuilder().createButton(root_.rootNodeId());
        auto secondNodeResult =
            context_->rootBuilder().createButton(root_.rootNodeId());
        ASSERT_TRUE(firstNodeResult.has_value());
        ASSERT_TRUE(secondNodeResult.has_value());
        firstNode_ = *firstNodeResult;
        secondNode_ = *secondNodeResult;

        auto gamepadsResult = GamepadPool::Create(1);
        ASSERT_TRUE(gamepadsResult.has_value());
        gamepads_ = std::make_unique<GamepadPool>(std::move(*gamepadsResult));
        auto gamepadResult = gamepads_->tryEmplace(1);
        ASSERT_TRUE(gamepadResult.has_value());
        gamepad_ = *gamepadResult;

        state_.emplace(ownerWindow_);
    }

    [[nodiscard]] static Platform::DigitalControlIdentity
    key(Platform::WindowId window, Platform::Key value)
    {
        return Platform::KeyControlIdentity{window, value};
    }

    [[nodiscard]] static Platform::DigitalControlIdentity
    button(Platform::WindowId window, Platform::GamepadId gamepad,
           Platform::GamepadButton value)
    {
        return Platform::GamepadButtonControlIdentity{
            .routedWindow = window,
            .gamepad = gamepad,
            .button = value,
        };
    }

    std::unique_ptr<WindowPool> windows_;
    std::unique_ptr<GamepadPool> gamepads_;
    std::unique_ptr<UI::UIContext> context_;
    UI::UIRootOwner root_;
    std::optional<PressState> state_;
    Platform::WindowId ownerWindow_{};
    Platform::WindowId otherWindow_{};
    Platform::GamepadId gamepad_{};
    UI::UINodeId firstNode_{};
    UI::UINodeId secondNode_{};
};

TEST_F(UIDefaultActionPressStateTests, ValidatesSourceOwnerAndAcceptedControls)
{
    for (const Platform::Key accepted : {
             Platform::Key::Enter,
             Platform::Key::Space,
             Platform::Key::KeypadEnter,
         })
    {
        EXPECT_TRUE(state_->validateControl(
            UI::UIButtonActivationSource::Keyboard, key(ownerWindow_, accepted)));
    }
    EXPECT_TRUE(state_->validateControl(
        UI::UIButtonActivationSource::Gamepad,
        button(ownerWindow_, gamepad_, Platform::GamepadButton::South)));

    const auto expectInvalid = [](Core::Status status) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidButtonAction);
    };
    expectInvalid(state_->validateControl(
        UI::UIButtonActivationSource::Keyboard,
        key(otherWindow_, Platform::Key::Enter)));
    expectInvalid(state_->validateControl(
        UI::UIButtonActivationSource::Keyboard,
        key(ownerWindow_, Platform::Key::A)));
    expectInvalid(state_->validateControl(
        UI::UIButtonActivationSource::Gamepad,
        button(ownerWindow_, gamepad_, Platform::GamepadButton::East)));
    expectInvalid(state_->validateControl(
        UI::UIButtonActivationSource::Keyboard,
        button(ownerWindow_, gamepad_, Platform::GamepadButton::South)));
    expectInvalid(state_->validateControl(
        UI::UIButtonActivationSource::Accessibility,
        key(ownerWindow_, Platform::Key::Enter)));
}

TEST_F(UIDefaultActionPressStateTests, TracksIndependentKeysAndClearsEveryPressForNode)
{
    const auto enter = key(ownerWindow_, Platform::Key::Enter);
    const auto space = key(ownerWindow_, Platform::Key::Space);
    const auto keypadEnter = key(ownerWindow_, Platform::Key::KeypadEnter);

    state_->setPressedTarget(enter, firstNode_);
    state_->setPressedTarget(space, secondNode_);
    state_->setPressedTarget(keypadEnter, firstNode_);
    EXPECT_EQ(state_->pressedTarget(enter), firstNode_);
    EXPECT_EQ(state_->pressedTarget(space), secondNode_);
    EXPECT_EQ(state_->pressedTarget(keypadEnter), firstNode_);
    EXPECT_TRUE(state_->isPressed(firstNode_));
    EXPECT_TRUE(state_->isPressed(secondNode_));

    state_->clearPressedTarget(enter);
    EXPECT_TRUE(state_->isPressed(firstNode_));
    state_->clearNode(firstNode_);
    EXPECT_FALSE(state_->isPressed(firstNode_));
    EXPECT_TRUE(state_->isPressed(secondNode_));

    state_->clearAll();
    EXPECT_FALSE(state_->isPressed(secondNode_));
}

TEST_F(UIDefaultActionPressStateTests, StaleGamepadGenerationCannotClearReusedSlot)
{
    const auto staleSouth =
        button(ownerWindow_, gamepad_, Platform::GamepadButton::South);
    state_->setPressedTarget(staleSouth, firstNode_);
    EXPECT_EQ(state_->pressedTarget(gamepad_), firstNode_);

    ASSERT_EQ(gamepads_->erase(gamepad_), Core::GenerationEraseResult::Erased);
    auto reusedResult = gamepads_->tryEmplace(2);
    ASSERT_TRUE(reusedResult.has_value());
    const Platform::GamepadId reused = *reusedResult;
    ASSERT_EQ(reused.index(), gamepad_.index());
    ASSERT_NE(reused.generation(), gamepad_.generation());
    const auto reusedSouth =
        button(ownerWindow_, reused, Platform::GamepadButton::South);

    state_->setPressedTarget(reusedSouth, secondNode_);
    EXPECT_FALSE(state_->pressedTarget(staleSouth).hasValue());
    EXPECT_EQ(state_->pressedTarget(reusedSouth), secondNode_);

    state_->clearPressedTarget(staleSouth);
    state_->clearGamepad(gamepad_);
    EXPECT_EQ(state_->pressedTarget(reused), secondNode_);
    state_->clearGamepad(reused);
    EXPECT_FALSE(state_->pressedTarget(reused).hasValue());
}

TEST_F(UIDefaultActionPressStateTests, CopySnapshotRestoresStateAndInvalidControlsDoNotMutate)
{
    const auto enter = key(ownerWindow_, Platform::Key::Enter);
    const auto south =
        button(ownerWindow_, gamepad_, Platform::GamepadButton::South);
    state_->setPressedTarget(enter, firstNode_);
    state_->setPressedTarget(south, secondNode_);
    const PressState snapshot = *state_;

    state_->setPressedTarget(key(otherWindow_, Platform::Key::Space), secondNode_);
    state_->setPressedTarget(
        button(ownerWindow_, gamepad_, Platform::GamepadButton::East),
        firstNode_);
    EXPECT_FALSE(state_->pressedTarget(
        key(ownerWindow_, Platform::Key::Space)).hasValue());
    EXPECT_EQ(state_->pressedTarget(south), secondNode_);

    state_->clearAll();
    EXPECT_FALSE(state_->isPressed(firstNode_));
    EXPECT_FALSE(state_->isPressed(secondNode_));
    *state_ = snapshot;
    EXPECT_EQ(state_->pressedTarget(enter), firstNode_);
    EXPECT_EQ(state_->pressedTarget(south), secondNode_);
}

} // namespace
} // namespace Tina::Tests
