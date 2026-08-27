#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] bool UIContext::Impl::isValidFlowLocalUser(UIFlowLocalUserId localUser) noexcept
{
    return localUser.hasValue() && localUser.value() <= UIFlowLocalUserCapacity;
}

[[nodiscard]] usize UIContext::Impl::flowLocalUserIndex(UIFlowLocalUserId localUser) noexcept
{
    return static_cast<usize>(localUser.value() - 1U);
}

[[nodiscard]] Core::Status UIContext::Impl::validateFlowLocalUser(UIFlowLocalUserId localUser) const
{
    if (!isValidFlowLocalUser(localUser))
    {
        return fail(UIErrorCode::InvalidFlowLocalUser,
                    "UI Flow local user is outside the fixed local-user capacity");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateFlowGamepad(Platform::GamepadId gamepad) const
{
    if (!gamepad.hasValue() || gamepad.index() >= flowGamepadAssignments.size())
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow Gamepad identity is outside the Platform slot capacity");
    }
    return Core::success();
}

[[nodiscard]] UIFlowLocalUserId
UIContext::Impl::flowLocalUserForGamepadUnchecked(Platform::GamepadId gamepad) const noexcept
{
    const UIFlowGamepadAssignment& assignment =
        flowGamepadAssignments[gamepad.index()];
    return assignment.gamepad == gamepad ? assignment.localUser
                                          : UIFlowPrimaryLocalUser;
}

[[nodiscard]] Core::Status UIContext::Impl::fallbackFlowInputDevicesForGamepads(
    Platform::GamepadId first,
    std::optional<Platform::GamepadId> second)
{
    const auto matches = [first, second](const UIFlowInputDeviceState& state) noexcept {
        return state.device == UIFlowInputDevice::Gamepad &&
               state.gamepad.has_value() &&
               (*state.gamepad == first ||
                (second.has_value() && *state.gamepad == *second));
    };
    for (const UIFlowInputDeviceState& state : observedFlowInputDevices)
    {
        if (matches(state) && state.revision == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Flow input-device revision is exhausted");
        }
    }
    for (UIFlowInputDeviceState& state : observedFlowInputDevices)
    {
        if (!matches(state))
        {
            continue;
        }
        state.device = UIFlowInputDevice::KeyboardMouse;
        state.gamepad.reset();
        ++state.revision;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::fallbackAllFlowInputDevices()
{
    for (const UIFlowInputDeviceState& state : observedFlowInputDevices)
    {
        if (state.device == UIFlowInputDevice::Gamepad &&
            state.revision == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Flow input-device revision is exhausted");
        }
    }
    for (UIFlowInputDeviceState& state : observedFlowInputDevices)
    {
        if (state.device != UIFlowInputDevice::Gamepad)
        {
            continue;
        }
        state.device = UIFlowInputDevice::KeyboardMouse;
        state.gamepad.reset();
        ++state.revision;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::assignFlowGamepad(
    Platform::GamepadId gamepad, UIFlowLocalUserId localUser)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
    {
        return validUser;
    }
    if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
    {
        return validGamepad;
    }

    UIFlowGamepadAssignment& assignment = flowGamepadAssignments[gamepad.index()];
    if (assignment.gamepad == gamepad && assignment.localUser == localUser)
    {
        return Core::success();
    }
    const std::optional<Platform::GamepadId> replacedGamepad =
        assignment.gamepad.hasValue()
            ? std::optional<Platform::GamepadId>{assignment.gamepad}
            : std::nullopt;
    if (Core::Status fallback =
            fallbackFlowInputDevicesForGamepads(gamepad, replacedGamepad);
        !fallback)
    {
        return fallback;
    }
    assignment = UIFlowGamepadAssignment{
        .gamepad = gamepad,
        .localUser = localUser,
    };
    return Core::success();
}

[[nodiscard]] Core::Status
UIContext::Impl::clearFlowGamepadAssignment(Platform::GamepadId gamepad)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
    {
        return validGamepad;
    }

    UIFlowGamepadAssignment& assignment = flowGamepadAssignments[gamepad.index()];
    if (assignment.gamepad != gamepad)
    {
        return Core::success();
    }
    if (Core::Status fallback = fallbackFlowInputDevicesForGamepads(gamepad);
        !fallback)
    {
        return fallback;
    }
    assignment = {};
    return Core::success();
}

[[nodiscard]] Core::Result<UIFlowLocalUserId>
UIContext::Impl::flowLocalUserForGamepad(Platform::GamepadId gamepad) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
    {
        return Core::failure(validGamepad.error());
    }
    return flowLocalUserForGamepadUnchecked(gamepad);
}

[[nodiscard]] Core::Result<UIFlowInputDeviceState>
UIContext::Impl::flowInputDeviceState(UIFlowLocalUserId localUser) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
    {
        return Core::failure(validUser.error());
    }
    return observedFlowInputDevices[flowLocalUserIndex(localUser)];
}

[[nodiscard]] Core::Status UIContext::Impl::cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                          std::optional<Platform::GamepadId> gamepad)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!routedWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI default-action cancellation requires a Window");
    }
    if (routedWindow != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI default-action cancellation belongs to another Window");
    }

    if (gamepad.has_value())
    {
        if (Core::Status validGamepad = validateFlowGamepad(*gamepad);
            !validGamepad)
        {
            return validGamepad;
        }
        if (Core::Status fallback = fallbackFlowInputDevicesForGamepads(*gamepad);
            !fallback)
        {
            return fallback;
        }
        UIFlowGamepadAssignment& assignment =
            flowGamepadAssignments[gamepad->index()];
        if (assignment.gamepad == *gamepad)
        {
            assignment = {};
        }
    }
    else
    {
        if (Core::Status fallback = fallbackAllFlowInputDevices(); !fallback)
        {
            return fallback;
        }
        for (UIFlowGamepadAssignment& assignment : flowGamepadAssignments)
        {
            assignment = {};
        }
    }

    dropdownCommandPressLatch.clear();
    listViewCommandPressLatch.clear();
    virtualGridViewCommandPressLatch.clear();
    dataGridCommandPressLatch.clear();
    treeViewCommandPressLatch.clear();
    focusNavigationPressLatch.clear();
    tabViewCommandPressLatch.clear();
    tabViewDirectionPressLatch.clear();

    if (gamepad.has_value())
    {
        rangeInputPressLatch.clearGamepad(*gamepad);
        flowActionPressState.clearGamepad(*gamepad);
        const UINodeId cancelledTarget = defaultActionPressState.pressedTarget(*gamepad);
        defaultActionPressState.clearGamepad(*gamepad);
        if (cancelledTarget.hasValue() && contains(cancelledTarget) && !isButtonPressed(cancelledTarget))
        {
            static_cast<void>(markPaintDirty(cancelledTarget));
        }
        return Core::success();
    }

    const UINodeId cancelledTarget = defaultActionFocusButton;
    rangeInputPressLatch.clear();
    flowActionPressState.clearAll();
    defaultActionPressState.clearAll();
    if (cancelledTarget.hasValue() && contains(cancelledTarget))
    {
        static_cast<void>(markPaintDirty(cancelledTarget));
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::observeFlowInputDevice(
    Platform::PlatformFrameId platformFrame, u64 sourceSequence,
    UIFlowLocalUserId localUser, UIFlowInputDevice device,
    std::optional<Platform::GamepadId> gamepad)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
    {
        return validUser;
    }
    UIFlowInputDeviceState& observedFlowInputDevice =
        observedFlowInputDevices[flowLocalUserIndex(localUser)];
    if (!platformFrame.hasValue() || sourceSequence == 0 ||
        sourceSequence <= observedFlowInputDevice.sourceSequence)
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow input-device observation must be strictly ordered");
    }
    if ((device == UIFlowInputDevice::KeyboardMouse && gamepad.has_value()) ||
        (device == UIFlowInputDevice::Gamepad &&
         (!gamepad.has_value() || !gamepad->hasValue())))
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow input-device observation has an invalid Gamepad identity");
    }
    if (device == UIFlowInputDevice::Gamepad)
    {
        if (Core::Status validGamepad = validateFlowGamepad(*gamepad);
            !validGamepad)
        {
            return validGamepad;
        }
        if (flowLocalUserForGamepadUnchecked(*gamepad) != localUser)
        {
            return fail(UIErrorCode::InvalidFlowLocalUser,
                        "UI Flow Gamepad observation does not match its local-user assignment");
        }
    }

    const bool changed = device != observedFlowInputDevice.device ||
                         gamepad != observedFlowInputDevice.gamepad;
    if (changed && observedFlowInputDevice.revision ==
                       (std::numeric_limits<u64>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI Flow input-device revision is exhausted");
    }

    observedFlowInputDevice.device = device;
    observedFlowInputDevice.gamepad = gamepad;
    observedFlowInputDevice.platformFrame = platformFrame;
    observedFlowInputDevice.sourceSequence = sourceSequence;
    if (changed)
    {
        ++observedFlowInputDevice.revision;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIFlowActionRouteResult>
UIContext::Impl::routeFlowAction(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                UIFlowLocalUserId localUser, UIFlowAction action,
                UIFlowActionSource source, bool pressed,
                const Platform::DigitalControlIdentity& control)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Flow action cannot nest during input routing");
    }
    drainDeferredRootDestroys();
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidFlowAction,
                    "UI Flow action requires a platform frame and source sequence");
    }
    if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
    {
        return Core::failure(validUser.error());
    }
    if (Core::Status valid = flowActionPressState.validate(action, source, control);
        !valid)
    {
        return Core::failure(valid.error());
    }
    const UIInputModality modality = source == UIFlowActionSource::Gamepad
                                         ? UIInputModality::Gamepad
                                         : UIInputModality::Keyboard;
    if (Core::Status modalityStatus = setInputModality(modality); !modalityStatus)
    {
        return Core::failure(modalityStatus.error());
    }
    if (source == UIFlowActionSource::Keyboard &&
        localUser != UIFlowPrimaryLocalUser)
    {
        return fail(UIErrorCode::InvalidFlowLocalUser,
                    "UI Flow keyboard actions belong to the primary local user");
    }
    if (source == UIFlowActionSource::Gamepad)
    {
        const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        if (button == nullptr ||
            flowLocalUserForGamepadUnchecked(button->gamepad) != localUser)
        {
            return fail(UIErrorCode::InvalidFlowLocalUser,
                        "UI Flow Gamepad action does not match its local-user assignment");
        }
    }

    const bool alreadyPressed = flowActionPressState.isPressed(action, control);
    if (!pressed)
    {
        if (alreadyPressed)
        {
            flowActionPressState.clearPressed(action, control);
        }
        return UIFlowActionRouteResult{.consumed = alreadyPressed};
    }
    if (alreadyPressed)
    {
        return UIFlowActionRouteResult{.consumed = true};
    }

    UINodeId targetScreen{};
    const auto& committedLayout = committedLayoutBuffers[publishedLayoutBufferIndex];
    for (auto entry = committedLayout.rbegin(); entry != committedLayout.rend(); ++entry)
    {
        const UINodeId candidate = entry->node;
        if (entry->effectiveVisibility != UIVisibility::Visible || !contains(candidate) ||
            candidate.index() >= flowStatesByNodeIndex.size())
        {
            continue;
        }
        const UIFlowNodeState& state = flowStatesByNodeIndex[candidate.index()];
        if (state.kind == UIFlowNodeKind::Screen && isActiveFlowScreenIndex(candidate.index()))
        {
            targetScreen = candidate;
            break;
        }
    }
    if (!targetScreen.hasValue())
    {
        return UIFlowActionRouteResult{};
    }

    UIFlowNodeState& targetState = flowStatesByNodeIndex[targetScreen.index()];
    const usize actionIndex = flowActionSlotIndex(action);
    if (actionIndex >= FlowActionSlotCount)
    {
        return fail(UIErrorCode::InvalidFlowAction,
                    "UI Flow action is not supported by this router");
    }
    UIFlowActionSlot& actionSlot = targetState.actions[actionIndex];
    if (!actionSlot.registered || !actionSlot.callback.hasValue())
    {
        return UIFlowActionRouteResult{.screen = UIFlowScreenId{targetScreen}};
    }

    flowActionPressState.setPressed(action, control);
    ++flowActionCallbackOperationDepth;
    UIFlowActionCallback callback = std::move(actionSlot.callback);
    --flowActionCallbackOperationDepth;
    ++flowActionInvocationCount;
    ++routeDispatchDepth;
    auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
        finishRoutedPointerDispatch();
    });
    callback(UIFlowActionEvent{
        .screen = UIFlowScreenId{targetScreen},
        .localUser = localUser,
        .action = action,
        .source = source,
        .platformFrame = platformFrame,
        .sourceSequence = sourceSequence,
    });
    if (contains(targetScreen) && targetScreen.index() < flowStatesByNodeIndex.size())
    {
        UIFlowNodeState& liveState = flowStatesByNodeIndex[targetScreen.index()];
        UIFlowActionSlot& liveActionSlot = liveState.actions[actionIndex];
        if (liveState.kind == UIFlowNodeKind::Screen && liveActionSlot.registered &&
            !liveActionSlot.callback.hasValue())
        {
            ++flowActionCallbackOperationDepth;
            liveActionSlot.callback = std::move(callback);
            --flowActionCallbackOperationDepth;
        }
    }
    return UIFlowActionRouteResult{
        .consumed = true,
        .invoked = true,
        .screen = UIFlowScreenId{targetScreen},
    };
}

[[nodiscard]] Core::Result<UIDefaultActionResult>
UIContext::Impl::routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIButtonActivationSource source,
                           std::optional<Platform::DigitalControlIdentity> control,
                           UINodeId explicitAccessibilityTarget)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI default-action activation cannot nest during routing");
    }
    drainDeferredRootDestroys();
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput,
                    "UI default-action activation requires a platform frame and sequence");
    }
    if (source != UIButtonActivationSource::Keyboard && source != UIButtonActivationSource::Gamepad &&
        source != UIButtonActivationSource::Accessibility)
    {
        return fail(UIErrorCode::InvalidButtonAction,
                    "UI default-action activation source must be Keyboard, Gamepad, or Accessibility");
    }
    const UIInputModality modality = source == UIButtonActivationSource::Gamepad
                                         ? UIInputModality::Gamepad
                                         : source == UIButtonActivationSource::Accessibility
                                               ? UIInputModality::Accessibility
                                               : UIInputModality::Keyboard;
    if (Core::Status modalityStatus = setInputModality(modality); !modalityStatus)
    {
        return Core::failure(modalityStatus.error());
    }
    if (explicitAccessibilityTarget.hasValue() &&
        (source != UIButtonActivationSource::Accessibility || control.has_value()))
    {
        return fail(UIErrorCode::InvalidButtonAction,
                    "UI explicit activation targets are reserved for accessibility actions");
    }
    if (control.has_value())
    {
        if (Core::Status validControl = defaultActionPressState.validateControl(source, *control);
            !validControl)
        {
            return Core::failure(validControl.error());
        }
        const UINodeId existingTarget = defaultActionPressState.pressedTarget(*control);
        if (existingTarget.hasValue())
        {
            const NodeRecord* existingRecord =
                contains(existingTarget) ? nodes.tryGet(existingTarget.storageId()) : nullptr;
            if ((existingTarget == defaultActionFocusButton &&
                 isCommittedKeyboardFocusCandidate(existingTarget)) ||
                (existingRecord != nullptr && existingRecord->kind == BuiltinElementKind::DropdownItem))
            {
                // Native key repeat and duplicate gamepad Down remain owned
                // by UI without re-running toggle/callback side effects.
                return UIDefaultActionResult{
                    .consumed = true,
                    .activated = false,
                };
            }
            defaultActionPressState.clearPressedTarget(*control);
        }
    }
    UINodeId activationTarget = explicitAccessibilityTarget;
    if (!activationTarget.hasValue() &&
        !isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
    {
        if (textInputFocus == defaultActionFocusButton)
        {
            clearImeFocus();
        } else
        {
            clearDefaultActionFocus();
        }
        return UIDefaultActionResult{};
    }
    if (!activationTarget.hasValue())
    {
        activationTarget = defaultActionFocusButton;
    }
    const NodeRecord* record = contains(activationTarget)
                                   ? nodes.tryGet(activationTarget.storageId())
                                   : nullptr;
    if (!explicitAccessibilityTarget.hasValue() && record != nullptr &&
        record->kind == BuiltinElementKind::TextEdit)
    {
        if (textInputFocus != activationTarget)
        {
            clearDefaultActionFocus();
            return UIDefaultActionResult{};
        }
        return UIDefaultActionResult{.consumed = true, .activated = false};
    }
    if (record == nullptr || !isNodeEnabled(activationTarget) ||
        !behaviorStateStorage.hasActivate(activationTarget.index()))
    {
        if (explicitAccessibilityTarget.hasValue())
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI accessibility activation target is not Activate-capable");
        }
        clearDefaultActionFocus();
        return UIDefaultActionResult{};
    }

    if (buttonRouteSerial == (std::numeric_limits<u64>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI Button route serial is exhausted");
    }
    const bool pressedStateChanges = control.has_value() && !isButtonPressed(activationTarget);
    releaseRouteDirtyQueueReservations();
    auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
    if (pressedStateChanges || behaviorStateStorage.hasToggle(activationTarget.index()))
    {
        addRouteDirtyReservationCandidate(activationTarget);
    }
    if (record->kind == BuiltinElementKind::RadioButton)
    {
        const NodeRecord* parent = recordByIndex(record->parentIndex);
        if (parent == nullptr)
        {
            return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
        }
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == activationTarget.index()))
            {
                addRouteDirtyReservationCandidate(idForIndex(childIndex));
            }
            childIndex = nextSiblingIndex;
        }
    }
    addDropdownActivationDirtyReservationCandidates(activationTarget);
    addMenuActivationDirtyReservationCandidates(activationTarget);
    addTabActivationDirtyReservationCandidates(activationTarget);
    if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
    {
        return Core::failure(reservation.error());
    }
    if (Core::Status dirty = preflightDefaultActionActivationDirty(activationTarget, pressedStateChanges); !dirty)
    {
        return Core::failure(dirty.error());
    }
    if (pressedStateChanges)
    {
        if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
        {
            return Core::failure(dirty.error());
        }
    }
    if (u8* toggleValue = behaviorStateStorage.tryToggleValue(activationTarget.index());
        toggleValue != nullptr)
    {
        if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
        {
            return Core::failure(dirty.error());
        }
        *toggleValue = *toggleValue == 0 ? 1 : 0;
    }
    if (record->kind == BuiltinElementKind::RadioButton)
    {
        if (Core::Status selected = applyRadioButtonSelection(activationTarget, true); !selected)
        {
            return Core::failure(selected.error());
        }
    }
    bool dropdownActivated = false;
    if (record->kind == BuiltinElementKind::Dropdown || record->kind == BuiltinElementKind::DropdownItem)
    {
        auto activated = activateDropdownControl(activationTarget);
        if (!activated)
        {
            return Core::failure(activated.error());
        }
        dropdownActivated = *activated;
    }
    bool tabActivated = false;
    bool menuItemActivated = false;
    if (record->kind == BuiltinElementKind::MenuItem)
    {
        auto activated = activateMenuItem(activationTarget);
        if (!activated)
        {
            return Core::failure(activated.error());
        }
        menuItemActivated = *activated;
    }
    if (record->kind == BuiltinElementKind::Tab)
    {
        auto activated = activateTabControl(activationTarget);
        if (!activated)
        {
            return Core::failure(activated.error());
        }
        tabActivated = *activated;
    }
    if (control.has_value())
    {
        defaultActionPressState.setPressedTarget(*control, activationTarget);
    }
    const u64 actionRegistrationSerialBoundary = buttonActionRegistry.registrationSerial();
    const Detail::UIButtonActionInvocation actionCandidate =
        isSubmenuMenuItem(activationTarget)
            ? Detail::UIButtonActionInvocation{}
            : captureButtonAction(activationTarget,
                                  actionRegistrationSerialBoundary);
    if (!actionCandidate.hasValue())
    {
        // Focused control without a registered action still consumes Accept
        // so gameplay does not also fire. Selection controls already
        // changed state above; a bare Button without a callback did not.
        const bool activated =
            behaviorStateStorage.hasToggle(activationTarget.index()) ||
            record->kind == BuiltinElementKind::RadioButton || dropdownActivated ||
            menuItemActivated || tabActivated;
        return UIDefaultActionResult{.consumed = true, .activated = activated};
    }
    const u64 currentButtonRouteSerial = ++buttonRouteSerial;
    ++routeDispatchDepth;
    auto dispatchCleanup = Core::makeScopeExit([this]() noexcept { finishRoutedPointerDispatch(); });
    invokeButtonAction(actionCandidate,
                       UIButtonActionEvent{
                           .buttonNode = activationTarget,
                           .source = source,
                           .platformFrame = platformFrame,
                           .sourceSequence = sourceSequence,
                       },
                       currentButtonRouteSerial);
    return UIDefaultActionResult{.consumed = true, .activated = true};
}

[[nodiscard]] Core::Status UIContext::Impl::performAccessibilityAction(const UIAccessibilityAction& action)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI accessibility action cannot nest during routing");
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(action.node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    NodeRecord* record = *nodeResult;
    if (!isNodeEnabled(action.node))
    {
        return fail(UIErrorCode::InvalidAccessibilityAction,
                    "UI accessibility action target is disabled");
    }

    UISemanticsAction requiredAction = UISemanticsAction::None;
    switch (action.kind)
    {
    case UIAccessibilityActionKind::Focus:
        requiredAction = UISemanticsAction::Focus;
        break;
    case UIAccessibilityActionKind::Invoke:
        requiredAction = UISemanticsAction::Activate;
        break;
    case UIAccessibilityActionKind::Toggle:
        requiredAction = UISemanticsAction::Toggle;
        break;
    case UIAccessibilityActionKind::SetRangeValue:
        requiredAction = UISemanticsAction::SetRangeValue;
        break;
    case UIAccessibilityActionKind::SetTextValue:
        requiredAction = UISemanticsAction::SetTextValue;
        break;
    default:
        return fail(UIErrorCode::InvalidAccessibilityAction,
                    "UI accessibility action kind is not recognized");
    }
    const auto& committedSemantics = committedSemanticsBuffers[publishedSemanticsBufferIndex];
    const auto semanticsEntry =
        std::find_if(committedSemantics.begin(), committedSemantics.end(), [&](const UISemanticsEntry& entry) {
            return entry.node == action.node;
        });
    if (semanticsEntry == committedSemantics.end() ||
        !hasSemanticsAction(semanticsEntry->actions, requiredAction))
    {
        return fail(UIErrorCode::InvalidAccessibilityAction,
                    "UI accessibility action is not published for the target semantics node");
    }

    switch (action.kind)
    {
    case UIAccessibilityActionKind::Focus:
        if (Core::Status modality = setInputModality(UIInputModality::Accessibility); !modality)
        {
            return modality;
        }
        return requestFocus(action.node);
    case UIAccessibilityActionKind::Invoke:
        if (!behaviorStateStorage.hasActivate(action.node.index()))
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility Invoke requires Activate behavior");
        }
        break;
    case UIAccessibilityActionKind::Toggle:
        if (!behaviorStateStorage.hasToggle(action.node.index()) &&
            !hasBehavior(record->behaviors, UIElementBehavior::ExclusiveChoice) &&
            record->kind != BuiltinElementKind::MenuItem)
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility Toggle requires toggle behavior");
        }
        break;
    case UIAccessibilityActionKind::SetRangeValue: {
        const Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(action.node.index());
        if (!hasBehavior(record->behaviors, UIElementBehavior::RangeInput) ||
            range == nullptr ||
            action.node.index() >= semanticsStatesByNodeIndex.size() ||
            semanticsStatesByNodeIndex[action.node.index()].readOnly || !std::isfinite(action.rangeValue) ||
            action.rangeValue < static_cast<double>(range->minValue) ||
            action.rangeValue > static_cast<double>(range->maxValue) ||
            action.rangeValue < static_cast<double>((std::numeric_limits<float>::lowest)()) ||
            action.rangeValue > static_cast<double>((std::numeric_limits<float>::max)()))
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility range value is invalid or read-only for the target RangeInput");
        }
        auto applied = applySliderValue(action.node, action.rangeValue, {}, 0, true);
        if (!applied)
        {
            return Core::failure(applied.error());
        }
        return Core::success();
    }
    case UIAccessibilityActionKind::SetTextValue: {
        if (!hasBehavior(record->behaviors, UIElementBehavior::TextInput) ||
            record->kind != BuiltinElementKind::TextEdit)
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility text value requires a TextEdit");
        }
        const UINodeId root = idForIndex(record->rootIndex);
        return setTextFromUpdater(root, action.node, action.textValue);
    }
    }

    if (accessibilityActionSequence == (std::numeric_limits<u64>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI accessibility action sequence is exhausted");
    }
    if (hasBehavior(record->behaviors, UIElementBehavior::Focusable))
    {
        if (Core::Status focus = requestFocus(action.node); !focus)
        {
            return focus;
        }
    }
    const u64 actionSequence = ++accessibilityActionSequence;
    if (action.kind == UIAccessibilityActionKind::Toggle &&
        behaviorStateStorage.hasToggle(action.node.index()) &&
        !behaviorStateStorage.hasActivate(action.node.index()))
    {
        u8* toggleValue = behaviorStateStorage.tryToggleValue(action.node.index());
        if (toggleValue == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI accessibility Toggle state is unavailable");
        }
        if (Core::Status dirty = markPaintDirty(action.node); !dirty)
        {
            return dirty;
        }
        *toggleValue = *toggleValue == 0 ? 1 : 0;
        return Core::success();
    }
    auto activated = routeDefaultActionActivate(
        Platform::PlatformFrameId{1}, actionSequence,
        UIButtonActivationSource::Accessibility, std::nullopt, action.node);
    if (!activated)
    {
        return Core::failure(activated.error());
    }
    if (!activated->consumed)
    {
        return fail(UIErrorCode::InvalidAccessibilityAction,
                    "UI accessibility action target could not be activated");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIDefaultActionResult>
UIContext::Impl::routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                          UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput,
                    "UI default-action release requires a platform frame and sequence");
    }
    if (Core::Status validControl = defaultActionPressState.validateControl(source, control);
        !validControl)
    {
        return Core::failure(validControl.error());
    }
    const UIInputModality modality = source == UIButtonActivationSource::Gamepad
                                         ? UIInputModality::Gamepad
                                         : source == UIButtonActivationSource::Accessibility
                                               ? UIInputModality::Accessibility
                                               : UIInputModality::Keyboard;
    if (Core::Status modalityStatus = setInputModality(modality); !modalityStatus)
    {
        return Core::failure(modalityStatus.error());
    }

    const UINodeId releasedTarget = defaultActionPressState.pressedTarget(control);
    if (!releasedTarget.hasValue())
    {
        return UIDefaultActionResult{};
    }

    defaultActionPressState.clearPressedTarget(control);
    if (contains(releasedTarget) && !isButtonPressed(releasedTarget))
    {
        // Physical Up cannot be replayed. Keep release as a successful
        // input barrier even when repaint capacity is temporarily full.
        static_cast<void>(markPaintDirty(releasedTarget));
    }
    return UIDefaultActionResult{
        .consumed = true,
        .activated = false,
    };
}

[[nodiscard]] Core::Status UIContext::Impl::applyExplicitFocus(UINodeId nextFocus)
{
    const UINodeId previousFocus = defaultActionFocusButton;
    const UINodeId previousTextFocus = textInputFocus;
    if (previousFocus == nextFocus &&
        ((nextFocus.hasValue() && isLiveTextEdit(nextFocus)) ? previousTextFocus == nextFocus
                                                             : !previousTextFocus.hasValue()))
    {
        return Core::success();
    }

    if (Core::Status dirty = preflightPaintDirtyBatch({
            previousFocus,
            previousTextFocus,
            nextFocus,
        });
        !dirty)
    {
        return dirty;
    }
    if (Core::Status dirty = markPaintDirtyBatch({
            previousFocus,
            previousTextFocus,
            nextFocus,
        });
        !dirty)
    {
        return dirty;
    }

    defaultActionPressState.clearAll();
    if (previousTextFocus != nextFocus)
    {
        resetImeCompositionState();
    }
    defaultActionFocusButton = nextFocus;
    const NodeRecord* nextRecord =
        nextFocus.hasValue() && contains(nextFocus) ? nodes.tryGet(nextFocus.storageId()) : nullptr;
    textInputFocus = nextRecord != nullptr && nextRecord->kind == BuiltinElementKind::TextEdit ? nextFocus : UINodeId{};
    if (previousTextFocus != textInputFocus)
    {
        resetTextEditPreferredX(previousTextFocus);
        resetTextEditPreferredX(textInputFocus);
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::requestFocus(UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI focus cannot be changed during pointer routing");
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isCommittedKeyboardFocusCandidate(node))
    {
        return fail(UIErrorCode::InvalidFocusTarget,
                    "UI focus target is not an enabled committed keyboard-focus candidate");
    }
    return applyExplicitFocus(node);
}

[[nodiscard]] Core::Status UIContext::Impl::clearFocus()
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI focus cannot be cleared during pointer routing");
    }
    drainDeferredRootDestroys();
    return applyExplicitFocus({});
}

[[nodiscard]] Core::Status UIContext::Impl::requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI focus target is not owned by the updater root");
    }
    return requestFocus(node);
}

[[nodiscard]] Core::Status UIContext::Impl::clearFocusFromUpdater(UINodeId updaterRoot)
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (defaultActionFocusButton.hasValue() && !isNodeWithinRoot(updaterRoot, defaultActionFocusButton))
    {
        return Core::success();
    }
    return clearFocus();
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::focusedNodeFromUpdater(UINodeId updaterRoot) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    const UINodeId focus = defaultActionFocus();
    if (!focus.hasValue() || !isNodeWithinRoot(updaterRoot, focus))
    {
        return UINodeId{};
    }
    return focus;
}

[[nodiscard]] UINodeId UIContext::Impl::activeFocusScope() const noexcept
{
    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    const u32 focusEntryIndex = findHitEntryIndex(defaultActionFocus(), entries);
    if (focusEntryIndex < entries.size())
    {
        const u32 scopeEntryIndex = entries[focusEntryIndex].focusScopeEntryIndex;
        if (scopeEntryIndex < entries.size())
        {
            return entries[scopeEntryIndex].node;
        }
    }
    return activeModalNode;
}

[[nodiscard]] UINodeId UIContext::Impl::activeModal() const noexcept
{
    return activeModalNode;
}

[[nodiscard]] UINodeId UIContext::Impl::pointerCapture() const noexcept
{
    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    return isPointerCaptureCandidate(capturedPointerNode, entries, committedActiveModalEntryIndex)
               ? capturedPointerNode
               : UINodeId{};
}

[[nodiscard]] UINodeId UIContext::Impl::activePopup() const noexcept
{
    if (!activePopupNode.hasValue() || !contains(activePopupNode) ||
        activePopupNode.index() >= popupStatesByNodeIndex.size() ||
        !popupStatesByNodeIndex[activePopupNode.index()].open)
    {
        return {};
    }
    const NodeRecord* record = nodes.tryGet(activePopupNode.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::Popup ? activePopupNode : UINodeId{};
}

[[nodiscard]] UINodeId UIContext::Impl::activeMenu() const noexcept
{
    const UINodeId menu = menuStorage.activeMenu();
    if (!contains(menu))
    {
        return {};
    }
    const NodeRecord* record = nodes.tryGet(menu.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::Menu
               ? menu
               : UINodeId{};
}

[[nodiscard]] Core::Result<UIDropdownCommandResult> UIContext::Impl::routeDropdownCommand(UIDropdownCommand command,
                                                                         bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Dropdown command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();

    if (!dropdownCommandPressLatch.accepts(command))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Dropdown command is not recognized");
    }

    if (!pressed)
    {
        return UIDropdownCommandResult{
            .consumed = dropdownCommandPressLatch.release(command),
            .changed = false,
            .focus = defaultActionFocus(),
        };
    }
    if (dropdownCommandPressLatch.isLatched(command))
    {
        return UIDropdownCommandResult{
            .consumed = true,
            .changed = false,
            .focus = defaultActionFocus(),
        };
    }

    const UINodeId popup = activePopup();
    const UINodeId dropdown = dropdownForPopup(popup);
    if (!popup.hasValue() || !dropdown.hasValue())
    {
        return UIDropdownCommandResult{};
    }

    if (command == UIDropdownCommand::Dismiss)
    {
        if (Core::Status closed = setPopupOpenState(popup, false); !closed)
        {
            return Core::failure(closed.error());
        }
        dropdownCommandPressLatch.latch(command);
        return UIDropdownCommandResult{
            .consumed = true,
            .changed = true,
            .focus = defaultActionFocus(),
        };
    }

    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    if (command == UIDropdownCommand::PreviousItem || command == UIDropdownCommand::NextItem)
    {
        const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
        if (select == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
        }
        const bool next = command == UIDropdownCommand::NextItem;
        u32 firstCandidate = InvalidUIHitEntryIndex;
        u32 lastCandidate = InvalidUIHitEntryIndex;
        u32 previousCandidate = InvalidUIHitEntryIndex;
        u32 nextCandidate = InvalidUIHitEntryIndex;
        u32 selectedCandidate = InvalidUIHitEntryIndex;
        bool currentFound = false;
        for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
        {
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (!contains(entry.node) || !hasBehavior(entry.behaviors, UIElementBehavior::SelectOption) ||
                dropdownForItem(entry.node) != dropdown || !isNodeEnabled(entry.node) ||
                entry.policy != UIPointerHitPolicy::Targetable ||
                !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex))
            {
                continue;
            }
            if (firstCandidate == InvalidUIHitEntryIndex)
            {
                firstCandidate = entryIndex;
            }
            lastCandidate = entryIndex;
            if (entry.node == select->selectedOption)
            {
                selectedCandidate = entryIndex;
            }
            if (currentFound && nextCandidate == InvalidUIHitEntryIndex)
            {
                nextCandidate = entryIndex;
            }
            if (entry.node == defaultActionFocusButton)
            {
                currentFound = true;
            } else if (!currentFound)
            {
                previousCandidate = entryIndex;
            }
        }

        u32 targetEntryIndex = InvalidUIHitEntryIndex;
        if (!currentFound)
        {
            targetEntryIndex = selectedCandidate != InvalidUIHitEntryIndex
                                   ? selectedCandidate
                                   : (next ? firstCandidate : lastCandidate);
        } else if (next)
        {
            targetEntryIndex = nextCandidate != InvalidUIHitEntryIndex
                                   ? nextCandidate
                                   : findHitEntryIndex(defaultActionFocusButton, entries);
        } else
        {
            targetEntryIndex = previousCandidate != InvalidUIHitEntryIndex
                                   ? previousCandidate
                                   : findHitEntryIndex(defaultActionFocusButton, entries);
        }

        bool changed = false;
        if (targetEntryIndex < entries.size())
        {
            const UINodeId nextFocus = entries[targetEntryIndex].node;
            changed = nextFocus != defaultActionFocusButton;
            if (changed)
            {
                if (Core::Status focused = applyExplicitFocus(nextFocus); !focused)
                {
                    return Core::failure(focused.error());
                }
            }
        }
        dropdownCommandPressLatch.latch(command);
        return UIDropdownCommandResult{
            .consumed = true,
            .changed = changed,
            .focus = defaultActionFocus(),
        };
    }

    const u32 dropdownEntryIndex = findHitEntryIndex(dropdown, entries);
    u32 firstCandidate = InvalidUIHitEntryIndex;
    u32 lastCandidate = InvalidUIHitEntryIndex;
    u32 previousCandidate = InvalidUIHitEntryIndex;
    u32 nextCandidate = InvalidUIHitEntryIndex;
    if (dropdownEntryIndex < entries.size())
    {
        const u32 scopeEntryIndex = entries[dropdownEntryIndex].focusScopeEntryIndex;
        for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
        {
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (!contains(entry.node) || entry.node == dropdown || isNodeWithinSubtree(popup, entry.node) ||
                !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
                !hasBehavior(entry.behaviors, UIElementBehavior::Focusable) ||
                !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
                !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
            {
                continue;
            }
            if (firstCandidate == InvalidUIHitEntryIndex)
            {
                firstCandidate = entryIndex;
            }
            lastCandidate = entryIndex;
            if (entryIndex < dropdownEntryIndex)
            {
                previousCandidate = entryIndex;
            } else if (entryIndex > dropdownEntryIndex && nextCandidate == InvalidUIHitEntryIndex)
            {
                nextCandidate = entryIndex;
            }
        }
    }

    const bool exitNext = command == UIDropdownCommand::ExitNext;
    const u32 exitEntryIndex = exitNext
                                   ? (nextCandidate != InvalidUIHitEntryIndex ? nextCandidate : firstCandidate)
                                   : (previousCandidate != InvalidUIHitEntryIndex ? previousCandidate : lastCandidate);
    const UINodeId exitFocus = exitEntryIndex < entries.size() ? entries[exitEntryIndex].node : dropdown;
    const UINodeId previousFocus = defaultActionFocusButton;
    releaseRouteDirtyQueueReservations();
    auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
    addRouteLayoutDirtyReservationCandidates(popup);
    addRouteLayoutDirtyReservationCandidates(dropdown);
    addRouteDirtyReservationCandidate(previousFocus);
    addRouteDirtyReservationCandidate(textInputFocus);
    addRouteDirtyReservationCandidate(exitFocus);
    if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
    {
        return Core::failure(reservation.error());
    }
    if (Core::Status closed = setPopupOpenState(popup, false); !closed)
    {
        return Core::failure(closed.error());
    }
    if (defaultActionFocusButton != exitFocus)
    {
        if (Core::Status focused = applyExplicitFocus(exitFocus); !focused)
        {
            return Core::failure(focused.error());
        }
    }
    dropdownCommandPressLatch.latch(command);
    return UIDropdownCommandResult{
        .consumed = true,
        .changed = true,
        .focus = defaultActionFocus(),
    };
}

[[nodiscard]] Core::Result<UIListViewCommandResult> UIContext::Impl::routeListViewCommand(UIListViewCommand command,
                                                                         bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI ListView command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();

    if (!listViewCommandPressLatch.accepts(command))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView command is not recognized");
    }
    if (!pressed)
    {
        return UIListViewCommandResult{
            .consumed = listViewCommandPressLatch.release(command)};
    }
    if (listViewCommandPressLatch.isLatched(command))
    {
        return UIListViewCommandResult{.consumed = true};
    }

    UINodeId listView = defaultActionFocusButton;
    const NodeRecord* focusRecord =
        contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
    if (focusRecord != nullptr && focusRecord->kind == BuiltinElementKind::ListViewItem)
    {
        listView = listViewForItem(listView);
        focusRecord = contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
    }
    if (focusRecord == nullptr || focusRecord->kind != BuiltinElementKind::ListView || !isNodeEnabled(listView))
    {
        return UIListViewCommandResult{};
    }

    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
    const auto consumeWithoutChange = [&]() noexcept {
        listViewCommandPressLatch.latch(command);
        return UIListViewCommandResult{
            .consumed = true,
            .selection = state.selection,
        };
    };
    if (command == UIListViewCommand::Activate)
    {
        listViewCommandPressLatch.latch(command);
        return UIListViewCommandResult{
            .consumed = true,
            .changed = false,
            .activated = state.selection.hasValue(),
            .selection = state.selection,
        };
    }
    if (itemCount == 0)
    {
        return consumeWithoutChange();
    }

    const u64 pageItems = (std::max)(
        u64{1}, static_cast<u64>(std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
    u64 candidate = 0;
    switch (command)
    {
    case UIListViewCommand::PreviousItem:
        candidate = state.selection.hasValue() && state.selection.logicalIndex != 0
                        ? state.selection.logicalIndex - 1
                        : 0;
        break;
    case UIListViewCommand::NextItem:
        candidate = state.selection.hasValue()
                        ? (std::min)(itemCount - 1, state.selection.logicalIndex + 1)
                        : 0;
        break;
    case UIListViewCommand::PreviousPage:
        candidate = state.selection.hasValue() && state.selection.logicalIndex > pageItems
                        ? state.selection.logicalIndex - pageItems
                        : 0;
        break;
    case UIListViewCommand::NextPage:
        candidate = state.selection.hasValue()
                        ? (std::min)(itemCount - 1, state.selection.logicalIndex + pageItems)
                        : 0;
        break;
    case UIListViewCommand::FirstItem:
        candidate = 0;
        break;
    case UIListViewCommand::LastItem:
        candidate = itemCount - 1;
        break;
    case UIListViewCommand::Activate:
        break;
    }

    const bool searchBackward = command == UIListViewCommand::PreviousItem ||
                                command == UIListViewCommand::PreviousPage ||
                                command == UIListViewCommand::LastItem;
    UIListViewItemDescriptor descriptor{};
    bool found = false;
    for (u64 visited = 0; visited < itemCount; ++visited)
    {
        auto resolved = resolveListViewLogicalItem(listView, candidate);
        if (!resolved)
        {
            return Core::failure(resolved.error());
        }
        if (resolved->enabled)
        {
            descriptor = *resolved;
            found = true;
            break;
        }
        if (searchBackward)
        {
            if (candidate == 0)
            {
                break;
            }
            --candidate;
        } else
        {
            if (candidate + 1 >= itemCount)
            {
                break;
            }
            ++candidate;
        }
    }
    if (!found)
    {
        return consumeWithoutChange();
    }

    const UIListViewSelection nextSelection{.key = descriptor.key, .logicalIndex = candidate};
    const bool changed = state.selection != nextSelection;
    if (changed)
    {
        if (Core::Status dirty = markPaintDirty(listView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.selection = nextSelection;
    }
    const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
    if (Core::Status revealed = scrollListViewToIndexFromUpdater(
            updaterRoot, listView, candidate, UIListViewScrollAlignment::Nearest);
        !revealed)
    {
        return Core::failure(revealed.error());
    }
    listViewCommandPressLatch.latch(command);
    return UIListViewCommandResult{
        .consumed = true,
        .changed = changed,
        .activated = false,
        .selection = state.selection,
    };
}

[[nodiscard]] Core::Result<UIVirtualGridViewCommandResult>
UIContext::Impl::routeVirtualGridViewCommand(UIVirtualGridViewCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI VirtualGridView command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();

    if (!virtualGridViewCommandPressLatch.accepts(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView command is not recognized");
    }
    if (!pressed)
    {
        return UIVirtualGridViewCommandResult{
            .consumed = virtualGridViewCommandPressLatch.release(command)};
    }
    if (virtualGridViewCommandPressLatch.isLatched(command))
    {
        return UIVirtualGridViewCommandResult{.consumed = true};
    }

    UINodeId virtualGridView = defaultActionFocusButton;
    const NodeRecord* focusRecord =
        contains(virtualGridView) ? nodes.tryGet(virtualGridView.storageId()) : nullptr;
    if (focusRecord != nullptr &&
        focusRecord->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        virtualGridView = virtualGridViewForItem(virtualGridView);
        focusRecord = contains(virtualGridView)
                          ? nodes.tryGet(virtualGridView.storageId())
                          : nullptr;
    }
    if (focusRecord == nullptr ||
        focusRecord->kind != BuiltinElementKind::VirtualGridView ||
        !isNodeEnabled(virtualGridView))
    {
        return UIVirtualGridViewCommandResult{};
    }

    VirtualGridViewState* statePointer =
        virtualGridViewStorage.tryView(virtualGridView);
    if (statePointer == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView state is unavailable");
    }
    VirtualGridViewState& state = *statePointer;
    const u64 itemCount = state.dataSource.hasValue()
                              ? state.dataSource.itemCount(state.dataSource.state)
                              : 0;
    const auto plan = resolveVirtualGridViewCommandNavigation(
        command, state, itemCount);
    if (!plan)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView command navigation shape is invalid");
    }
    if (plan->activateCurrentSelection)
    {
        bool activated = false;
        if (state.selection.hasValue())
        {
            auto resolved = resolveVirtualGridViewLogicalItem(
                virtualGridView, state.selection.logicalIndex);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            activated = resolved->enabled &&
                        resolved->key == state.selection.key;
        }
        virtualGridViewCommandPressLatch.latch(command);
        return UIVirtualGridViewCommandResult{
            .consumed = true,
            .activated = activated,
            .selection = state.selection,
        };
    }
    if (!plan->hasTarget)
    {
        virtualGridViewCommandPressLatch.latch(command);
        return UIVirtualGridViewCommandResult{
            .consumed = true,
            .selection = state.selection,
        };
    }

    const bool searchByRow =
        command == UIVirtualGridViewCommand::PreviousRow ||
        command == UIVirtualGridViewCommand::NextRow ||
        command == UIVirtualGridViewCommand::PreviousPage ||
        command == UIVirtualGridViewCommand::NextPage;
    const bool searchBackward =
        command == UIVirtualGridViewCommand::PreviousItem ||
        command == UIVirtualGridViewCommand::PreviousRow ||
        command == UIVirtualGridViewCommand::PreviousPage ||
        command == UIVirtualGridViewCommand::LastItem;
    u32 logicalColumnCount = state.committedMetrics.logicalColumnCount;
    if (itemCount < logicalColumnCount)
    {
        logicalColumnCount = static_cast<u32>(itemCount);
    }
    const u64 searchStride = searchByRow ? logicalColumnCount : u64{1};
    u64 candidateIndex = plan->targetIndex;
    std::optional<UIVirtualGridViewItemDescriptor> candidateDescriptor{};
    for (u64 visited = 0; visited < itemCount; ++visited)
    {
        auto resolved = resolveVirtualGridViewLogicalItem(
            virtualGridView, candidateIndex);
        if (!resolved)
        {
            return Core::failure(resolved.error());
        }
        if (resolved->enabled)
        {
            candidateDescriptor = *resolved;
            break;
        }
        if (searchBackward)
        {
            if (candidateIndex < searchStride)
            {
                break;
            }
            candidateIndex -= searchStride;
        }
        else
        {
            if (searchStride > itemCount - 1 - candidateIndex)
            {
                break;
            }
            candidateIndex += searchStride;
        }
    }
    if (!candidateDescriptor.has_value())
    {
        virtualGridViewCommandPressLatch.latch(command);
        return UIVirtualGridViewCommandResult{
            .consumed = true,
            .selection = state.selection,
        };
    }

    const UIVirtualGridViewSelection nextSelection{
        .key = candidateDescriptor->key,
        .logicalIndex = candidateIndex,
        .logicalRow = candidateIndex / logicalColumnCount,
        .logicalColumn = static_cast<u32>(
            candidateIndex % logicalColumnCount),
    };
    const bool changed = state.selection != nextSelection;
    if (changed)
    {
        if (Core::Status dirty = markPaintDirty(virtualGridView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        static_cast<void>(virtualGridViewStorage.setSelection(
            virtualGridView, nextSelection));
    }
    const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
    if (Core::Status revealed = scrollVirtualGridViewToIndexFromUpdater(
            updaterRoot, virtualGridView, candidateIndex,
            UIVirtualGridViewScrollAlignment::Nearest);
        !revealed)
    {
        return Core::failure(revealed.error());
    }
    virtualGridViewCommandPressLatch.latch(command);
    return UIVirtualGridViewCommandResult{
        .consumed = true,
        .changed = changed,
        .selection = state.selection,
    };
}

[[nodiscard]] Core::Result<UIDataGridCommandResult>
UIContext::Impl::routeDataGridCommand(UIDataGridCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI DataGrid command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();

    if (!dataGridCommandPressLatch.accepts(command))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid command is not recognized");
    }
    if (!pressed)
    {
        return UIDataGridCommandResult{
            .consumed = dataGridCommandPressLatch.release(command)};
    }
    if (dataGridCommandPressLatch.isLatched(command))
    {
        return UIDataGridCommandResult{.consumed = true};
    }

    UINodeId dataGrid = defaultActionFocusButton;
    const NodeRecord* focusRecord =
        contains(dataGrid) ? nodes.tryGet(dataGrid.storageId()) : nullptr;
    if (focusRecord != nullptr &&
        focusRecord->kind == BuiltinElementKind::DataGridCell)
    {
        dataGrid = dataGridForCell(dataGrid);
        focusRecord = contains(dataGrid)
                          ? nodes.tryGet(dataGrid.storageId())
                          : nullptr;
    }
    if (focusRecord == nullptr ||
        focusRecord->kind != BuiltinElementKind::DataGrid ||
        !isNodeEnabled(dataGrid))
    {
        return UIDataGridCommandResult{};
    }

    DataGridState* statePointer = dataGridStorage.tryGrid(dataGrid);
    if (statePointer == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI DataGrid state is unavailable");
    }
    DataGridState& state = *statePointer;
    const u64 rowCount = state.dataSource.hasValue()
                             ? state.dataSource.rowCount(state.dataSource.state)
                             : 0;
    const u32 columnCount = state.dataSource.hasValue()
                                ? state.dataSource.columnCount(
                                      state.dataSource.state)
                                : 0;
    if (columnCount > state.columnCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid logical columns exceed the fixed column pool");
    }
    const auto plan = resolveDataGridCommandNavigation(
        command, state, rowCount, columnCount);
    if (!plan)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid command navigation shape is invalid");
    }
    const auto consumeWithoutChange = [&]() noexcept {
        dataGridCommandPressLatch.latch(command);
        return UIDataGridCommandResult{
            .consumed = true,
            .selection = state.selection,
        };
    };
    if (plan->activateCurrentSelection)
    {
        bool selectedRowEnabled = false;
        UIDataGridSelection resolvedSelection{};
        if (state.selection.hasValue())
        {
            auto resolved = resolveDataGridSelection(
                dataGrid, state.selection.logicalRow,
                state.selection.logicalColumn, &selectedRowEnabled);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            resolvedSelection = *resolved;
        }
        const bool activated = selectedRowEnabled &&
                               resolvedSelection.hasValue() &&
                               resolvedSelection == state.selection;
        dataGridCommandPressLatch.latch(command);
        return UIDataGridCommandResult{
            .consumed = true,
            .activated = activated,
            .selection = state.selection,
        };
    }
    if (!plan->hasTarget)
    {
        return consumeWithoutChange();
    }

    const bool maySearchAnotherRow =
        !state.selection.hasValue() ||
        command == UIDataGridCommand::PreviousRow ||
        command == UIDataGridCommand::NextRow ||
        command == UIDataGridCommand::PreviousPage ||
        command == UIDataGridCommand::NextPage ||
        command == UIDataGridCommand::FirstCell ||
        command == UIDataGridCommand::LastCell;
    const bool searchBackward =
        command == UIDataGridCommand::PreviousRow ||
        command == UIDataGridCommand::PreviousPage ||
        command == UIDataGridCommand::LastCell;
    u64 candidateRow = plan->targetRow;
    std::optional<UIDataGridSelection> candidateSelection{};
    for (u64 visited = 0; visited < rowCount; ++visited)
    {
        bool rowEnabled = false;
        auto resolved = resolveDataGridSelection(
            dataGrid, candidateRow, plan->targetColumn, &rowEnabled);
        if (!resolved)
        {
            return Core::failure(resolved.error());
        }
        if (rowEnabled)
        {
            candidateSelection = *resolved;
            break;
        }
        if (!maySearchAnotherRow)
        {
            break;
        }
        if (searchBackward)
        {
            if (candidateRow == 0)
            {
                break;
            }
            --candidateRow;
        }
        else
        {
            if (candidateRow + 1 >= rowCount)
            {
                break;
            }
            ++candidateRow;
        }
    }
    if (!candidateSelection.has_value())
    {
        return consumeWithoutChange();
    }

    const bool changed = state.selection != *candidateSelection;
    if (changed)
    {
        if (Core::Status dirty = markPaintDirty(dataGrid); !dirty)
        {
            return Core::failure(dirty.error());
        }
        static_cast<void>(dataGridStorage.setSelection(
            dataGrid, *candidateSelection));
    }
    const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
    if (Core::Status revealed = scrollDataGridToCellFromUpdater(
            updaterRoot, dataGrid, candidateSelection->logicalRow,
            candidateSelection->logicalColumn,
            UIDataGridScrollAlignment::Nearest);
        !revealed)
    {
        return Core::failure(revealed.error());
    }
    dataGridCommandPressLatch.latch(command);
    return UIDataGridCommandResult{
        .consumed = true,
        .changed = changed,
        .selection = state.selection,
    };
}

[[nodiscard]] UINodeId UIContext::Impl::rootForVirtualGridView(UINodeId virtualGridView) const noexcept
{
    const NodeRecord* record = nodes.tryGet(virtualGridView.storageId());
    return record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewDataSource(
    UINodeId virtualGridView, UIVirtualGridViewDataSource source)
{
    return setVirtualGridViewDataSourceFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView, source);
}

[[nodiscard]] Core::Status UIContext::Impl::clearVirtualGridViewDataSource(UINodeId virtualGridView)
{
    return clearVirtualGridViewDataSourceFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateVirtualGridViewItems(UINodeId virtualGridView)
{
    return invalidateVirtualGridViewItemsFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewStyle(
    UINodeId virtualGridView, const UIVirtualGridViewStyle& style)
{
    return setVirtualGridViewStyleFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView, style);
}

[[nodiscard]] Core::Result<UIVirtualGridViewStyle>
UIContext::Impl::virtualGridViewStyle(UINodeId virtualGridView) const
{
    return virtualGridViewStyleFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewPaint(
    UINodeId virtualGridView, const UIVirtualGridViewPaint& paint)
{
    return setVirtualGridViewPaintFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView, paint);
}

[[nodiscard]] Core::Result<UIVirtualGridViewPaint>
UIContext::Impl::virtualGridViewPaint(UINodeId virtualGridView) const
{
    return virtualGridViewPaintFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Result<UIVirtualGridViewMetrics>
UIContext::Impl::virtualGridViewMetrics(UINodeId virtualGridView) const
{
    return virtualGridViewMetricsFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewSelectedIndex(
    UINodeId virtualGridView, u64 logicalIndex)
{
    return setVirtualGridViewSelectedIndexFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView, logicalIndex);
}

[[nodiscard]] Core::Status UIContext::Impl::clearVirtualGridViewSelection(UINodeId virtualGridView)
{
    return clearVirtualGridViewSelectionFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Result<UIVirtualGridViewSelection>
UIContext::Impl::virtualGridViewSelection(UINodeId virtualGridView) const
{
    return virtualGridViewSelectionFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::scrollVirtualGridViewToIndex(
    UINodeId virtualGridView, u64 logicalIndex,
    UIVirtualGridViewScrollAlignment alignment)
{
    return scrollVirtualGridViewToIndexFromUpdater(
        rootForVirtualGridView(virtualGridView), virtualGridView,
        logicalIndex, alignment);
}

[[nodiscard]] UINodeId UIContext::Impl::rootForDataGrid(UINodeId dataGrid) const noexcept
{
    const NodeRecord* record = nodes.tryGet(dataGrid.storageId());
    return record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridDataSource(
    UINodeId dataGrid, UIDataGridDataSource source)
{
    return setDataGridDataSourceFromUpdater(
        rootForDataGrid(dataGrid), dataGrid, source);
}

[[nodiscard]] Core::Status UIContext::Impl::clearDataGridDataSource(UINodeId dataGrid)
{
    return clearDataGridDataSourceFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateDataGridItems(UINodeId dataGrid)
{
    return invalidateDataGridItemsFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridStyle(
    UINodeId dataGrid, const UIDataGridStyle& style)
{
    return setDataGridStyleFromUpdater(
        rootForDataGrid(dataGrid), dataGrid, style);
}

[[nodiscard]] Core::Result<UIDataGridStyle>
UIContext::Impl::dataGridStyle(UINodeId dataGrid) const
{
    return dataGridStyleFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridPaint(
    UINodeId dataGrid, const UIDataGridPaint& paint)
{
    return setDataGridPaintFromUpdater(
        rootForDataGrid(dataGrid), dataGrid, paint);
}

[[nodiscard]] Core::Result<UIDataGridPaint>
UIContext::Impl::dataGridPaint(UINodeId dataGrid) const
{
    return dataGridPaintFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Result<UIDataGridMetrics>
UIContext::Impl::dataGridMetrics(UINodeId dataGrid) const
{
    return dataGridMetricsFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridSelectedCell(
    UINodeId dataGrid, u64 logicalRow, u32 logicalColumn)
{
    return setDataGridSelectedCellFromUpdater(
        rootForDataGrid(dataGrid), dataGrid, logicalRow, logicalColumn);
}

[[nodiscard]] Core::Status UIContext::Impl::clearDataGridSelection(UINodeId dataGrid)
{
    return clearDataGridSelectionFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Result<UIDataGridSelection>
UIContext::Impl::dataGridSelection(UINodeId dataGrid) const
{
    return dataGridSelectionFromUpdater(
        rootForDataGrid(dataGrid), dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::scrollDataGridToCell(
    UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
    UIDataGridScrollAlignment alignment)
{
    return scrollDataGridToCellFromUpdater(
        rootForDataGrid(dataGrid), dataGrid, logicalRow, logicalColumn,
        alignment);
}

[[nodiscard]] Core::Result<UITreeViewCommandResult> UIContext::Impl::routeTreeViewCommand(UITreeViewCommand command, bool pressed)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI TreeView command cannot run during pointer routing");
    }
    drainDeferredRootDestroys();

    if (!treeViewCommandPressLatch.accepts(command))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView command is not recognized");
    }
    if (!pressed)
    {
        return UITreeViewCommandResult{
            .consumed = treeViewCommandPressLatch.release(command)};
    }
    if (treeViewCommandPressLatch.isLatched(command))
    {
        return UITreeViewCommandResult{.consumed = true};
    }

    UINodeId treeView = defaultActionFocusButton;
    const NodeRecord* focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
    if (focusRecord != nullptr && focusRecord->kind == BuiltinElementKind::TreeViewItem)
    {
        treeView = treeViewForItem(treeView);
        focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
    }
    if (focusRecord == nullptr || focusRecord->kind != BuiltinElementKind::TreeView || !isNodeEnabled(treeView))
    {
        return UITreeViewCommandResult{};
    }

    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
    const auto consumeWithoutChange = [&]() noexcept {
        treeViewCommandPressLatch.latch(command);
        return UITreeViewCommandResult{
            .consumed = true,
            .selection = state.selection,
        };
    };
    u64 selectedIndex = 0;
    UITreeViewItemDescriptor selectedDescriptor{};
    bool hasSelectedItem = false;
    if (state.selection.hasValue())
    {
        if (state.selection.logicalIndex < itemCount)
        {
            auto resolved = resolveTreeViewLogicalItem(treeView, state.selection.logicalIndex);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->key == state.selection.key)
            {
                selectedIndex = state.selection.logicalIndex;
                selectedDescriptor = *resolved;
                hasSelectedItem = true;
            }
        }
        if (!hasSelectedItem)
        {
            for (u64 logicalIndex = 0; logicalIndex < itemCount; ++logicalIndex)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->key == state.selection.key)
                {
                    selectedIndex = logicalIndex;
                    selectedDescriptor = *resolved;
                    hasSelectedItem = true;
                    break;
                }
            }
        }
    }

    if (command == UITreeViewCommand::Activate)
    {
        if (!hasSelectedItem || !selectedDescriptor.enabled)
        {
            return consumeWithoutChange();
        }
        treeViewCommandPressLatch.latch(command);
        return UITreeViewCommandResult{
            .consumed = true,
            .activated = true,
            .selection = state.selection,
        };
    }
    if (itemCount == 0)
    {
        return consumeWithoutChange();
    }

    const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
    auto applyExpansion = [&](bool expanded) -> Core::Result<UITreeViewCommandResult> {
        if (!hasSelectedItem || !selectedDescriptor.enabled || !selectedDescriptor.expandable)
        {
            return consumeWithoutChange();
        }
        if (Core::Status changed =
                setTreeViewItemExpandedFromUpdater(updaterRoot, treeView, selectedIndex, expanded);
            !changed)
        {
            return Core::failure(changed.error());
        }
        treeViewCommandPressLatch.latch(command);
        return UITreeViewCommandResult{
            .consumed = true,
            .changed = true,
            .expansionChanged = true,
            .selection = state.selection,
        };
    };

    if (command == UITreeViewCommand::ToggleExpanded)
    {
        return applyExpansion(!selectedDescriptor.expanded);
    }
    if (command == UITreeViewCommand::CollapseOrParent && hasSelectedItem && selectedDescriptor.enabled &&
        selectedDescriptor.expandable && selectedDescriptor.expanded)
    {
        return applyExpansion(false);
    }
    if (command == UITreeViewCommand::ExpandOrFirstChild && hasSelectedItem && selectedDescriptor.enabled &&
        selectedDescriptor.expandable && !selectedDescriptor.expanded)
    {
        return applyExpansion(true);
    }

    u64 candidate = 0;
    UITreeViewItemDescriptor candidateDescriptor{};
    bool found = false;
    bool searchBackward = false;
    switch (command)
    {
    case UITreeViewCommand::PreviousItem:
    case UITreeViewCommand::PreviousPage:
    case UITreeViewCommand::NextItem:
    case UITreeViewCommand::NextPage:
    case UITreeViewCommand::FirstItem:
    case UITreeViewCommand::LastItem: {
        const u64 pageItems =
            (std::max)(u64{1}, static_cast<u64>(
                                   std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
        switch (command)
        {
        case UITreeViewCommand::PreviousItem:
            candidate = hasSelectedItem && selectedIndex != 0 ? selectedIndex - 1 : 0;
            break;
        case UITreeViewCommand::NextItem:
            candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + 1) : 0;
            break;
        case UITreeViewCommand::PreviousPage:
            candidate = hasSelectedItem && selectedIndex > pageItems ? selectedIndex - pageItems : 0;
            break;
        case UITreeViewCommand::NextPage:
            candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + pageItems) : 0;
            break;
        case UITreeViewCommand::FirstItem:
            candidate = 0;
            break;
        case UITreeViewCommand::LastItem:
            candidate = itemCount - 1;
            break;
        default:
            break;
        }
        searchBackward = command == UITreeViewCommand::LastItem ||
                         (hasSelectedItem && (command == UITreeViewCommand::PreviousItem ||
                                              command == UITreeViewCommand::PreviousPage));
        for (u64 visited = 0; visited < itemCount; ++visited)
        {
            auto resolved = resolveTreeViewLogicalItem(treeView, candidate);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->enabled)
            {
                candidateDescriptor = *resolved;
                found = true;
                break;
            }
            if (searchBackward)
            {
                if (candidate == 0)
                {
                    break;
                }
                --candidate;
            } else
            {
                if (candidate + 1 >= itemCount)
                {
                    break;
                }
                ++candidate;
            }
        }
        break;
    }
    case UITreeViewCommand::CollapseOrParent: {
        if (!hasSelectedItem || selectedDescriptor.level == 0)
        {
            return consumeWithoutChange();
        }
        u32 ancestorLevel = selectedDescriptor.level;
        for (u64 logicalIndex = selectedIndex; logicalIndex-- > 0;)
        {
            auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->level >= ancestorLevel)
            {
                continue;
            }
            ancestorLevel = resolved->level;
            if (resolved->enabled)
            {
                candidate = logicalIndex;
                candidateDescriptor = *resolved;
                found = true;
                break;
            }
            if (ancestorLevel == 0)
            {
                break;
            }
        }
        break;
    }
    case UITreeViewCommand::ExpandOrFirstChild: {
        if (!hasSelectedItem)
        {
            return consumeWithoutChange();
        }
        const u64 childLevel = static_cast<u64>(selectedDescriptor.level) + 1;
        for (u64 logicalIndex = selectedIndex + 1; logicalIndex < itemCount; ++logicalIndex)
        {
            auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->level <= selectedDescriptor.level)
            {
                break;
            }
            if (static_cast<u64>(resolved->level) == childLevel && resolved->enabled)
            {
                candidate = logicalIndex;
                candidateDescriptor = *resolved;
                found = true;
                break;
            }
        }
        break;
    }
    case UITreeViewCommand::ToggleExpanded:
    case UITreeViewCommand::Activate:
        break;
    }

    if (!found)
    {
        return consumeWithoutChange();
    }
    const UITreeViewSelection nextSelection{
        .key = candidateDescriptor.key,
        .logicalIndex = candidate,
        .level = candidateDescriptor.level,
    };
    const bool changed = state.selection != nextSelection;
    if (changed)
    {
        if (Core::Status dirty = markPaintDirty(treeView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.selection = nextSelection;
    }
    if (Core::Status revealed =
            scrollTreeViewToIndexFromUpdater(updaterRoot, treeView, candidate, UITreeViewScrollAlignment::Nearest);
        !revealed)
    {
        return Core::failure(revealed.error());
    }
    treeViewCommandPressLatch.latch(command);
    return UITreeViewCommandResult{
        .consumed = true,
        .changed = changed,
        .selection = state.selection,
    };
}

[[nodiscard]] Core::Status UIContext::Impl::applyNavigationFocus(UINodeId nextFocus)
{
    const UINodeId previousFocus = defaultActionFocusButton;
    const UINodeId dirtyPreviousFocus =
        previousFocus.hasValue() && previousFocus != nextFocus && contains(previousFocus) ? previousFocus
                                                                                          : UINodeId{};
    const UINodeId dirtyTextFocus =
        textInputFocus.hasValue() && textInputFocus != nextFocus && contains(textInputFocus) ? textInputFocus
                                                                                             : UINodeId{};
    const UINodeId dirtyArmedSlider =
        armedSlider.hasValue() && armedSlider != nextFocus && contains(armedSlider) ? armedSlider : UINodeId{};
    if (Core::Status dirty = markPaintDirtyBatch({
            dirtyPreviousFocus,
            dirtyTextFocus,
            dirtyArmedSlider,
            nextFocus,
        });
        !dirty)
    {
        return dirty;
    }

    defaultActionPressState.clearAll();
    defaultActionFocusButton = nextFocus;
    clearArmedPrimaryButton();
    clearArmedSlider();
    clearArmedTextEdit();
    capturedPointerNode = {};
    const NodeRecord* nextRecord = nodes.tryGet(nextFocus.storageId());
    if (nextRecord != nullptr && nextRecord->kind == BuiltinElementKind::TextEdit)
    {
        if (textInputFocus != nextFocus)
        {
            clearImeFocus();
            resetTextEditPreferredX(nextFocus);
            textInputFocus = nextFocus;
        }
    } else
    {
        clearImeFocus();
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIDefaultFocusStepResult> UIContext::Impl::routeDefaultActionFocusStep(bool reverse)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI focus traversal cannot run during pointer routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status modalityStatus = setInputModality(UIInputModality::Keyboard);
        !modalityStatus)
    {
        return Core::failure(modalityStatus.error());
    }

    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    u32 scopeEntryIndex = committedActiveModalEntryIndex;
    const u32 currentFocusEntryIndex = findHitEntryIndex(defaultActionFocusButton, entries);
    if (currentFocusEntryIndex < entries.size() &&
        entries[currentFocusEntryIndex].focusScopeEntryIndex < entries.size())
    {
        scopeEntryIndex = entries[currentFocusEntryIndex].focusScopeEntryIndex;
    }

    u32 firstCandidateEntryIndex = InvalidUIHitEntryIndex;
    u32 lastCandidateEntryIndex = InvalidUIHitEntryIndex;
    u32 previousCandidateEntryIndex = InvalidUIHitEntryIndex;
    u32 nextCandidateEntryIndex = InvalidUIHitEntryIndex;
    bool currentFocusFound = false;
    usize candidateCount = 0;
    for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
    {
        const UICommittedHitEntry& entry = entries[entryIndex];
        if (!contains(entry.node) || !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
            !hasBehavior(entry.behaviors, UIElementBehavior::Focusable) ||
            !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
            !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
        {
            continue;
        }

        ++candidateCount;
        if (firstCandidateEntryIndex == InvalidUIHitEntryIndex)
        {
            firstCandidateEntryIndex = entryIndex;
        }
        lastCandidateEntryIndex = entryIndex;
        if (currentFocusFound && nextCandidateEntryIndex == InvalidUIHitEntryIndex)
        {
            nextCandidateEntryIndex = entryIndex;
        }
        if (entry.node == defaultActionFocusButton)
        {
            currentFocusFound = true;
        } else if (!currentFocusFound)
        {
            previousCandidateEntryIndex = entryIndex;
        }
    }
    if (candidateCount == 0)
    {
        if (Core::Status cleared = applyExplicitFocus({}); !cleared)
        {
            return Core::failure(cleared.error());
        }
        return UIDefaultFocusStepResult{};
    }

    const u32 nextEntryIndex =
        currentFocusFound
            ? (reverse ? (previousCandidateEntryIndex != InvalidUIHitEntryIndex ? previousCandidateEntryIndex
                                                                                : lastCandidateEntryIndex)
                       : (nextCandidateEntryIndex != InvalidUIHitEntryIndex ? nextCandidateEntryIndex
                                                                            : firstCandidateEntryIndex))
            : (reverse ? lastCandidateEntryIndex : firstCandidateEntryIndex);
    const UINodeId nextFocus = entries[nextEntryIndex].node;
    const bool moved = !defaultActionFocusButton.hasValue() || defaultActionFocusButton != nextFocus;
    if (Core::Status focused = applyNavigationFocus(nextFocus); !focused)
    {
        return Core::failure(focused.error());
    }
    return UIDefaultFocusStepResult{
        .consumed = true,
        .moved = moved,
        .focus = nextFocus,
    };
}

[[nodiscard]] Core::Result<UIRangeInputCommandResult>
UIContext::Impl::routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                       UIRangeInputCommand command, bool pressed,
                       const Platform::DigitalControlIdentity& control)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI RangeInput command cannot run during pointer routing");
    }
    if (!platformFrame.hasValue() || sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput,
                    "UI RangeInput command requires a platform frame and sequence");
    }
    if (command != UIRangeInputCommand::Decrease && command != UIRangeInputCommand::Increase)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI RangeInput command is not recognized");
    }
    if (Core::Status validControl = rangeInputPressLatch.validateControl(control); !validControl)
    {
        return Core::failure(validControl.error());
    }
    drainDeferredRootDestroys();

    if (!pressed)
    {
        return UIRangeInputCommandResult{
            .consumed = rangeInputPressLatch.release(control, command),
            .changed = false,
        };
    }
    if (rangeInputPressLatch.isLatched(control, command))
    {
        return UIRangeInputCommandResult{.consumed = true, .changed = false, .targeted = true};
    }

    const UINodeId target = defaultActionFocus();
    const NodeRecord* targetRecord = target.hasValue() ? nodes.tryGet(target.storageId()) : nullptr;
    const Detail::UIRangeInputState* state =
        target.hasValue() ? behaviorStateStorage.tryRangeInputState(target.index()) : nullptr;
    const bool targeted = targetRecord != nullptr && state != nullptr &&
                          hasBehavior(targetRecord->behaviors, UIElementBehavior::RangeInput);
    if (!targeted)
    {
        return UIRangeInputCommandResult{};
    }
    if (!isInteractiveRangeInput(target))
    {
        return UIRangeInputCommandResult{.targeted = true};
    }
    const double span = static_cast<double>(state->maxValue) - static_cast<double>(state->minValue);
    const double increment = state->step > 0.0F ? static_cast<double>(state->step) : span * 0.01;
    if (!(increment > 0.0) || !std::isfinite(increment))
    {
        return UIRangeInputCommandResult{.targeted = true};
    }
    const double requestedValue = static_cast<double>(state->value) +
                                  (command == UIRangeInputCommand::Increase ? increment : -increment);
    auto applied = applySliderValue(target, requestedValue, platformFrame, sourceSequence, true);
    if (!applied)
    {
        return Core::failure(applied.error());
    }
    if (!*applied)
    {
        return UIRangeInputCommandResult{.targeted = true};
    }
    rangeInputPressLatch.latch(control, command);
    return UIRangeInputCommandResult{.consumed = true, .changed = true, .targeted = true};
}

[[nodiscard]] Core::Result<UIDefaultFocusStepResult>
UIContext::Impl::routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed,
                     UIInputModality modality)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!isValidFocusNavigationDirection(direction))
    {
        return fail(UIErrorCode::InvalidFocusTarget, "UI focus navigation direction is not recognized");
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI focus navigation cannot run during pointer routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status modalityStatus = setInputModality(modality); !modalityStatus)
    {
        return Core::failure(modalityStatus.error());
    }

    const UINodeId currentFocus = defaultActionFocus();
    if (!pressed)
    {
        return UIDefaultFocusStepResult{
            .consumed = focusNavigationPressLatch.release(direction),
            .moved = false,
            .focus = currentFocus,
        };
    }
    if (focusNavigationPressLatch.isLatched(direction))
    {
        return UIDefaultFocusStepResult{
            .consumed = true,
            .moved = false,
            .focus = currentFocus,
        };
    }
    if (!currentFocus.hasValue())
    {
        return UIDefaultFocusStepResult{};
    }
    const NodeRecord* currentRecord = nodes.tryGet(currentFocus.storageId());
    if (currentRecord == nullptr || ownsDirectionalNavigation(currentRecord->kind))
    {
        return UIDefaultFocusStepResult{
            .consumed = false,
            .moved = false,
            .focus = currentFocus,
        };
    }

    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    u32 scopeEntryIndex = committedActiveModalEntryIndex;
    const u32 currentFocusEntryIndex = findHitEntryIndex(currentFocus, entries);
    if (currentFocusEntryIndex < entries.size() &&
        entries[currentFocusEntryIndex].focusScopeEntryIndex < entries.size())
    {
        scopeEntryIndex = entries[currentFocusEntryIndex].focusScopeEntryIndex;
    }
    const auto isCandidate = [&](u32 entryIndex) noexcept {
        const UICommittedHitEntry& entry = entries[entryIndex];
        const NodeRecord* record = nodes.tryGet(entry.node.storageId());
        return contains(entry.node) && isNodeEnabled(entry.node) &&
               record != nullptr && !isCompositeFocusItem(record->kind) &&
               entry.policy == UIPointerHitPolicy::Targetable &&
               hasBehavior(entry.behaviors, UIElementBehavior::Focusable) &&
               hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) &&
               hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries);
    };
    const u32 nextEntryIndex =
        findFocusNavigationCandidate(entries, currentFocusEntryIndex, direction, isCandidate);
    if (nextEntryIndex == InvalidUIHitEntryIndex)
    {
        return UIDefaultFocusStepResult{
            .consumed = false,
            .moved = false,
            .focus = currentFocus,
        };
    }

    const UINodeId nextFocus = entries[nextEntryIndex].node;
    const bool moved = nextFocus != currentFocus;
    if (Core::Status focused = applyNavigationFocus(nextFocus); !focused)
    {
        return Core::failure(focused.error());
    }
    focusNavigationPressLatch.latch(direction);
    return UIDefaultFocusStepResult{
        .consumed = true,
        .moved = moved,
        .focus = nextFocus,
    };
}

} // namespace Tina::UI
