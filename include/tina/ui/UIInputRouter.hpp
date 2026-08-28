#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UIAccessibility.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIFlow.hpp>
#include <tina/ui/UIFocus.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIMenu.hpp>
#include <tina/ui/UIRangeInput.hpp>
#include <tina/ui/UIRoutedPointerListenerToken.hpp>
#include <tina/ui/UITabView.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include <optional>

namespace Tina::UI {

class UIContext;

struct UIDefaultActionResult final {
    bool consumed = false;
    bool activated = false;
};

struct UIDefaultFocusStepResult final {
    bool consumed = false;
    bool moved = false;
    UINodeId focus{};
};

class UIInputRouter final {
  public:
    [[nodiscard]] UIPointerHitQueryResult
    queryPointerHit(UILogicalPoint point) const noexcept;
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                             UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Result<UIPointerRouteResult>
    routePointerInput(const UIPointerInputEvent& input);
    [[nodiscard]] Core::Status
    cancelPointerInteraction(Platform::WindowId routedWindow);
    [[nodiscard]] Core::Status cancelDefaultActionInteraction(
        Platform::WindowId routedWindow,
        std::optional<Platform::GamepadId> gamepad = std::nullopt);
    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionActivate(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIButtonActivationSource source,
        std::optional<Platform::DigitalControlIdentity> control = std::nullopt);
    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionRelease(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIButtonActivationSource source,
        const Platform::DigitalControlIdentity& control);
    [[nodiscard]] Core::Result<UIFlowActionRouteResult> routeFlowAction(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIFlowLocalUserId localUser, UIFlowAction action,
        UIFlowActionSource source, bool pressed,
        const Platform::DigitalControlIdentity& control);
    [[nodiscard]] Core::Status observeFlowInputDevice(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIFlowLocalUserId localUser, UIFlowInputDevice device,
        std::optional<Platform::GamepadId> gamepad = std::nullopt);
    [[nodiscard]] Core::Status assignFlowGamepad(
        Platform::GamepadId gamepad, UIFlowLocalUserId localUser);
    [[nodiscard]] Core::Status
    clearFlowGamepadAssignment(Platform::GamepadId gamepad);
    [[nodiscard]] Core::Result<UIFlowLocalUserId>
    flowLocalUserForGamepad(Platform::GamepadId gamepad) const;
    [[nodiscard]] Core::Result<UIFlowInputDeviceState>
    flowInputDeviceState(UIFlowLocalUserId localUser) const;
    [[nodiscard]] Core::Result<UIDefaultFocusStepResult>
    routeDefaultActionFocusStep(bool reverse);
    [[nodiscard]] Core::Result<UIDefaultFocusStepResult>
    routeFocusNavigation(
        UIFocusNavigationDirection direction, bool pressed = true,
        UIInputModality modality = UIInputModality::Keyboard);
    [[nodiscard]] Core::Result<UIRangeInputCommandResult>
    routeRangeInputCommand(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIRangeInputCommand command, bool pressed,
        const Platform::DigitalControlIdentity& control);
    [[nodiscard]] Core::Result<UIDropdownCommandResult>
    routeDropdownCommand(UIDropdownCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIListViewCommandResult>
    routeListViewCommand(UIListViewCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIVirtualGridViewCommandResult>
    routeVirtualGridViewCommand(UIVirtualGridViewCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIDataGridCommandResult>
    routeDataGridCommand(UIDataGridCommand command, bool pressed);
    [[nodiscard]] Core::Result<UITreeViewCommandResult>
    routeTreeViewCommand(UITreeViewCommand command, bool pressed);
    [[nodiscard]] Core::Result<UITabViewCommandResult>
    routeFocusedTabViewDirection(UIFocusNavigationDirection direction,
                                 bool pressed);
    [[nodiscard]] Core::Result<UITabViewCommandResult>
    routeFocusedTabViewCommand(UITabViewCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIMenuCommandResult>
    routeMenuCommand(UIMenuCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIMenuInvocationResult>
    routeMenuInvocation(UIMenuInvocationCommand command, bool pressed);
    [[nodiscard]] UINodeId defaultActionFocus() const noexcept;
    [[nodiscard]] UINodeId activeFocusScope() const noexcept;
    [[nodiscard]] UINodeId activeModal() const noexcept;
    [[nodiscard]] UINodeId pointerCapture(Platform::PointerId pointer) const noexcept;
    [[nodiscard]] UINodeId activePopup() const noexcept;
    [[nodiscard]] UINodeId activeMenu() const noexcept;
    [[nodiscard]] Core::Status requestFocus(UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
    [[nodiscard]] Core::Status
    performAccessibilityAction(const UIAccessibilityAction& action);

  private:
    friend class UIContext;

    explicit UIInputRouter(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
