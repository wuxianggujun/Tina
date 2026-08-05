#pragma once

#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UI.hpp>

#include "../../src/runtime/input/ActionMapper.hpp"
#include "../../src/runtime/input/LastPresentedCamera2DLatch.hpp"
#include "../../src/runtime/input/UIInputRouteProducer.hpp"

#include <memory>
#include <memory_resource>
#include <vector>

namespace Tina::Tests {

using Runtime::Input::ActionMapper;
using Runtime::Input::LastPresentedCamera2DLatch;
using Runtime::Input::UIInputRouteProducer;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

inline constexpr InputActionId PointerAction{41};
inline constexpr InputActionId NavigationAction{42};

struct FrameSpec final {
    Platform::PlatformFrameId frameId{1};
    std::vector<Platform::InputTransitionPayload> transitions{};
    std::vector<Platform::PlatformEventPayload> platformEvents{};
    std::vector<Platform::Key> heldKeys{};
    std::vector<Platform::PointerButton> heldPointerButtons{};
    double pointerX = 10.0;
    double pointerY = 10.0;
    double accumulatedDeltaX = 0.0;
    double accumulatedDeltaY = 0.0;
    std::vector<Platform::GamepadSnapshot> gamepadSnapshots{};
};

struct RouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId target{};
};

struct DropdownRouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId before{};
    UI::UINodeId dropdown{};
    UI::UINodeId popup{};
    UI::UINodeId firstItem{};
    UI::UINodeId secondItem{};
    UI::UINodeId after{};
};

struct ListRouteSource final {
    [[nodiscard]] UI::UIListViewDataSource view() const noexcept;
};

struct TreeRouteSource final {
    bool rootExpanded = true;

    [[nodiscard]] UI::UITreeViewDataSource view() noexcept;
};

struct CollectionRouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    std::unique_ptr<ListRouteSource> listSource;
    std::unique_ptr<TreeRouteSource> treeSource;
    UI::UINodeId listView{};
    UI::UINodeId treeView{};
    UI::UINodeId other{};
};

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept;
[[nodiscard]] Platform::PointerMoveTransition pointerMove(Platform::WindowId window, double x, double y,
                                                          double deltaX = 0.0, double deltaY = 0.0) noexcept;
[[nodiscard]] Platform::PointerButtonTransition
pointerButton(Platform::WindowId window, Platform::DigitalTransition state, double x, double y) noexcept;
[[nodiscard]] Platform::PointerWheelTransition pointerWheel(Platform::WindowId window, double x, double y,
                                                            double deltaX, double deltaY) noexcept;
[[nodiscard]] Platform::KeyTransition keyDown(Platform::WindowId window, Platform::Key key) noexcept;
[[nodiscard]] Platform::KeyTransition keyUp(Platform::WindowId window, Platform::Key key) noexcept;
[[nodiscard]] Platform::GamepadButtonTransition
gamepadButtonDown(Platform::WindowId window, Platform::GamepadId gamepad) noexcept;
[[nodiscard]] Platform::GamepadButtonTransition
gamepadButtonUp(Platform::WindowId window, Platform::GamepadId gamepad) noexcept;
[[nodiscard]] Platform::GamepadButtonTransition gamepadButton(
    Platform::WindowId window, Platform::GamepadId gamepad, Platform::GamepadButton button,
    Platform::DigitalTransition state) noexcept;
[[nodiscard]] Platform::GamepadSnapshot heldSouthSnapshot(Platform::GamepadId gamepad, u64 revision) noexcept;
[[nodiscard]] Platform::GamepadSnapshot releasedSouthSnapshot(Platform::GamepadId gamepad,
                                                              u64 revision) noexcept;

void expectOk(Core::Status status);
[[nodiscard]] UI::UIRoutedPointerListenerToken
addListener(UI::UIContext& context, UI::UIRoutedPointerListenerDesc descriptor,
            UI::UIRoutedPointerCallback callback);
[[nodiscard]] RouteTree createRouteTree(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 16,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
[[nodiscard]] RouteTree createTextEditRouteTree(Platform::WindowId window);
[[nodiscard]] DropdownRouteTree createDropdownRouteTree(Platform::WindowId window);
[[nodiscard]] CollectionRouteTree createCollectionRouteTree(Platform::WindowId window);
[[nodiscard]] Core::Result<Platform::PlatformFrameView>
buildFrame(Platform::PlatformFrameBuilder& builder, Platform::WindowId window, const FrameSpec& spec);
[[nodiscard]] std::unique_ptr<UIInputRouteProducer> createProducer(
    usize rawTransitionCapacity = 128,
    usize continuousControlClaimCapacity =
        InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
[[nodiscard]] std::unique_ptr<ActionMapper> createPointerMapper();
[[nodiscard]] std::unique_ptr<ActionMapper> createKeyMapper(Platform::Key key);
[[nodiscard]] const InputActionTransition* digital(const SimulationActionTransition& transition);

class UIInputRouteProducerTest : public testing::Test {
  protected:
    void SetUp() override;

    std::unique_ptr<WindowPool> windows;
    std::unique_ptr<GamepadPool> gamepads;
    std::unique_ptr<Platform::PlatformFrameBuilder> builder;
    LastPresentedCamera2DLatch lastPresentedCamera2D;
    Platform::WindowId window{};
    Platform::WindowId otherWindow{};
    Platform::GamepadId gamepad{};
};

} // namespace Tina::Tests
