#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIInputRouter.hpp>

namespace Tina::UI {

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
                                                           u32 slot, u32 generation) noexcept
    : m_lifetime(std::move(lifetime)), m_slot(slot), m_generation(generation)
{
}

UIRoutedPointerListenerToken::~UIRoutedPointerListenerToken() noexcept
{
    reset();
}

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

UIRoutedPointerListenerToken& UIRoutedPointerListenerToken::operator=(UIRoutedPointerListenerToken&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void UIRoutedPointerListenerToken::reset() noexcept
{
    const u32 generation = m_generation;
    if (generation == 0)
    {
        m_lifetime.reset();
        m_slot = 0;
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* immediateContext =
        lifetime ? lifetime->releaseRoutedPointerListener(m_slot, generation)
                 : nullptr;

    if (immediateContext != nullptr)
    {
        immediateContext->m_impl->releaseRoutedPointerListenerFromToken(m_slot, generation);
    }
    m_lifetime.reset();
    m_slot = 0;
    m_generation = 0;
}

bool UIRoutedPointerListenerToken::isActive() const noexcept
{
    if (m_generation == 0)
    {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        return false;
    }
    return lifetime->isRoutedPointerListenerActive(m_slot, m_generation);
}

UIRoutedPointerListenerToken::operator bool() const noexcept
{
    return isActive();
}

UIPointerHitQueryResult UIInputRouter::queryPointerHit(UILogicalPoint point) const noexcept
{
    return m_context->m_impl->queryPointerHit(point);
}

Core::Result<UIRoutedPointerListenerToken> UIInputRouter::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                               UIRoutedPointerCallback callback)
{
    auto registration = m_context->m_impl->addRoutedPointerListener(descriptor, std::move(callback), {});
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_context->m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Result<UIPointerRouteResult> UIInputRouter::routePointerInput(const UIPointerInputEvent& input)
{
    return m_context->m_impl->routePointerInput(input);
}

Core::Status UIInputRouter::cancelPointerInteraction(Platform::WindowId routedWindow,
                                                    std::optional<Platform::PointerId> pointer)
{
    return m_context->m_impl->cancelPointerInteraction(routedWindow, pointer);
}

Core::Status UIInputRouter::cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                       std::optional<Platform::GamepadId> gamepad)
{
    return m_context->m_impl->cancelDefaultActionInteraction(routedWindow, gamepad);
}

Core::Result<UIDefaultActionResult>
UIInputRouter::routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                      UIButtonActivationSource source,
                                      std::optional<Platform::DigitalControlIdentity> control)
{
    return m_context->m_impl->routeDefaultActionActivate(platformFrame, sourceSequence, source, std::move(control));
}

Core::Result<UIDefaultActionResult>
UIInputRouter::routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                     UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
{
    return m_context->m_impl->routeDefaultActionRelease(platformFrame, sourceSequence, source, control);
}

Core::Result<UIFlowActionRouteResult>
UIInputRouter::routeFlowAction(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIFlowLocalUserId localUser, UIFlowAction action,
                           UIFlowActionSource source, bool pressed,
                           const Platform::DigitalControlIdentity& control)
{
    return m_context->m_impl->routeFlowAction(platformFrame, sourceSequence, localUser, action,
                                   source, pressed, control);
}

Core::Status UIInputRouter::observeFlowInputDevice(
    Platform::PlatformFrameId platformFrame, u64 sourceSequence,
    UIFlowLocalUserId localUser, UIFlowInputDevice device,
    std::optional<Platform::GamepadId> gamepad)
{
    return m_context->m_impl->observeFlowInputDevice(platformFrame, sourceSequence, localUser,
                                          device, gamepad);
}

Core::Status UIInputRouter::assignFlowGamepad(Platform::GamepadId gamepad,
                                          UIFlowLocalUserId localUser)
{
    return m_context->m_impl->assignFlowGamepad(gamepad, localUser);
}

Core::Status UIInputRouter::clearFlowGamepadAssignment(Platform::GamepadId gamepad)
{
    return m_context->m_impl->clearFlowGamepadAssignment(gamepad);
}

Core::Result<UIFlowLocalUserId>
UIInputRouter::flowLocalUserForGamepad(Platform::GamepadId gamepad) const
{
    return m_context->m_impl->flowLocalUserForGamepad(gamepad);
}

Core::Result<UIFlowInputDeviceState>
UIInputRouter::flowInputDeviceState(UIFlowLocalUserId localUser) const
{
    return m_context->m_impl->flowInputDeviceState(localUser);
}

Core::Result<UIDefaultFocusStepResult> UIInputRouter::routeDefaultActionFocusStep(bool reverse)
{
    return m_context->m_impl->routeDefaultActionFocusStep(reverse);
}

Core::Result<UIDefaultFocusStepResult>
UIInputRouter::routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed,
                                UIInputModality modality)
{
    return m_context->m_impl->routeFocusNavigation(direction, pressed, modality);
}

Core::Result<UIRangeInputCommandResult>
UIInputRouter::routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                  UIRangeInputCommand command, bool pressed,
                                  const Platform::DigitalControlIdentity& control)
{
    return m_context->m_impl->routeRangeInputCommand(platformFrame, sourceSequence, command, pressed, control);
}

Core::Result<UIDropdownCommandResult> UIInputRouter::routeDropdownCommand(UIDropdownCommand command, bool pressed)
{
    return m_context->m_impl->routeDropdownCommand(command, pressed);
}

Core::Result<UIListViewCommandResult> UIInputRouter::routeListViewCommand(UIListViewCommand command, bool pressed)
{
    return m_context->m_impl->routeListViewCommand(command, pressed);
}

Core::Result<UIVirtualGridViewCommandResult>
UIInputRouter::routeVirtualGridViewCommand(UIVirtualGridViewCommand command, bool pressed)
{
    return m_context->m_impl->routeVirtualGridViewCommand(command, pressed);
}

Core::Result<UIDataGridCommandResult>
UIInputRouter::routeDataGridCommand(UIDataGridCommand command, bool pressed)
{
    return m_context->m_impl->routeDataGridCommand(command, pressed);
}

Core::Result<UITreeViewCommandResult> UIInputRouter::routeTreeViewCommand(UITreeViewCommand command, bool pressed)
{
    return m_context->m_impl->routeTreeViewCommand(command, pressed);
}

Core::Result<UITabViewCommandResult> UIInputRouter::routeFocusedTabViewDirection(
    UIFocusNavigationDirection direction, bool pressed)
{
    return m_context->m_impl->routeFocusedTabViewDirection(direction, pressed);
}

Core::Result<UITabViewCommandResult> UIInputRouter::routeFocusedTabViewCommand(
    UITabViewCommand command, bool pressed)
{
    return m_context->m_impl->routeFocusedTabViewCommand(command, pressed);
}

Core::Result<UIMenuCommandResult> UIInputRouter::routeMenuCommand(
    UIMenuCommand command, bool pressed)
{
    return m_context->m_impl->routeMenuCommand(command, pressed);
}

Core::Result<UIMenuInvocationResult> UIInputRouter::routeMenuInvocation(
    UIMenuInvocationCommand command, bool pressed)
{
    return m_context->m_impl->routeMenuInvocation(command, pressed);
}

UINodeId UIInputRouter::defaultActionFocus() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->defaultActionFocus();
}

UINodeId UIInputRouter::activeFocusScope() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->activeFocusScope();
}

UINodeId UIInputRouter::activeModal() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->activeModal();
}

UINodeId UIInputRouter::pointerCapture(Platform::PointerId pointer) const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->pointerCapture(pointer);
}

UINodeId UIInputRouter::activePopup() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->activePopup();
}

UINodeId UIInputRouter::activeMenu() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->activeMenu();
}

Core::Status UIInputRouter::requestFocus(UINodeId node)
{
    return m_context->m_impl->requestFocus(node);
}

Core::Status UIInputRouter::clearFocus()
{
    return m_context->m_impl->clearFocus();
}

Core::Status UIInputRouter::performAccessibilityAction(const UIAccessibilityAction& action)
{
    return m_context->m_impl->performAccessibilityAction(action);
}


} // namespace Tina::UI
