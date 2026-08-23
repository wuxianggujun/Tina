#include "UIInputRouteProducer.hpp"

#include "ActionMapper.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace Tina::Runtime::Input {
namespace {

constexpr usize BitsPerConsumptionWord = sizeof(u64) * 8U;
constexpr double PointerDeviceSwitchDistanceSquared = 4.0;

struct FlowInputDeviceObservation final {
    UI::UIFlowLocalUserId localUser = UI::UIFlowPrimaryLocalUser;
    UI::UIFlowInputDevice device = UI::UIFlowInputDevice::KeyboardMouse;
    std::optional<Platform::GamepadId> gamepad{};
};

[[nodiscard]] Core::Status invariantFailure(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation, message);
}

[[nodiscard]] constexpr usize consumptionWordCount(usize transitionCount) noexcept
{
    return (transitionCount + BitsPerConsumptionWord - 1U) / BitsPerConsumptionWord;
}

[[nodiscard]] Core::Result<std::optional<FlowInputDeviceObservation>>
flowInputDeviceObservation(const Platform::InputTransitionPayload& payload,
                           UI::UIContext& context)
{
    const Platform::WindowId ownerWindow = context.ownerWindow();
    if (const auto* key = std::get_if<Platform::KeyTransition>(&payload);
        key != nullptr && key->window == ownerWindow &&
        key->state == Platform::DigitalTransition::Down)
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    if (const auto* button = std::get_if<Platform::PointerButtonTransition>(&payload);
        button != nullptr && button->window == ownerWindow &&
        button->state == Platform::DigitalTransition::Down)
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    if (const auto* move = std::get_if<Platform::PointerMoveTransition>(&payload);
        move != nullptr && move->window == ownerWindow &&
        move->deltaX * move->deltaX + move->deltaY * move->deltaY >=
            PointerDeviceSwitchDistanceSquared)
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    if (const auto* wheel = std::get_if<Platform::PointerWheelTransition>(&payload);
        wheel != nullptr && wheel->window == ownerWindow &&
        (wheel->deltaX != 0.0 || wheel->deltaY != 0.0))
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    if (const auto* button = std::get_if<Platform::GamepadButtonTransition>(&payload);
        button != nullptr && button->routedWindow == ownerWindow &&
        button->state == Platform::DigitalTransition::Down)
    {
        auto localUser = context.flowLocalUserForGamepad(button->gamepad);
        if (!localUser)
        {
            return Core::failure(std::move(localUser.error()));
        }
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{
            .localUser = *localUser,
            .device = UI::UIFlowInputDevice::Gamepad,
            .gamepad = button->gamepad,
        }};
    }
    if (const auto* text = std::get_if<Platform::TextInputTransition>(&payload);
        text != nullptr && text->window == ownerWindow)
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    if (const auto* composition = std::get_if<Platform::TextCompositionTransition>(&payload);
        composition != nullptr && composition->window == ownerWindow &&
        composition->stage != Platform::TextCompositionStage::Cancelled)
    {
        return std::optional<FlowInputDeviceObservation>{FlowInputDeviceObservation{}};
    }
    return std::optional<FlowInputDeviceObservation>{};
}

[[nodiscard]] Core::Result<UI::UIFlowActionRouteResult> routeGamepadFlowAction(
    UI::UIContext& context, Platform::PlatformFrameId platformFrame,
    u64 sourceSequence, UI::UIFlowAction action,
    const Platform::GamepadButtonTransition& transition,
    const Platform::DigitalControlIdentity& control)
{
    auto localUser = context.flowLocalUserForGamepad(transition.gamepad);
    if (!localUser)
    {
        return Core::failure(std::move(localUser.error()));
    }
    return context.routeFlowAction(
        platformFrame, sourceSequence, *localUser, action,
        UI::UIFlowActionSource::Gamepad,
        transition.state == Platform::DigitalTransition::Down, control);
}

[[nodiscard]] bool isValidDigitalTransition(Platform::DigitalTransition state) noexcept
{
    return state == Platform::DigitalTransition::Down || state == Platform::DigitalTransition::Up;
}

[[nodiscard]] bool isValidPointerButton(Platform::PointerButton button) noexcept
{
    return button < Platform::PointerButton::Count;
}

[[nodiscard]] std::optional<UI::UITextEditCommand> textEditCommandForKey(
    Platform::Key key,
    bool controlHeld) noexcept
{
    if (controlHeld && key == Platform::Key::A) {
        return UI::UITextEditCommand::SelectAll;
    }
    switch (key) {
    case Platform::Key::Left:
        return UI::UITextEditCommand::MoveLeft;
    case Platform::Key::Right:
        return UI::UITextEditCommand::MoveRight;
    case Platform::Key::Up:
        return UI::UITextEditCommand::MoveUp;
    case Platform::Key::Down:
        return UI::UITextEditCommand::MoveDown;
    case Platform::Key::Home:
        return UI::UITextEditCommand::MoveHome;
    case Platform::Key::End:
        return UI::UITextEditCommand::MoveEnd;
    case Platform::Key::Backspace:
        return UI::UITextEditCommand::Backspace;
    case Platform::Key::Delete:
        return UI::UITextEditCommand::Delete;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIFocusNavigationDirection> focusNavigationDirectionForKey(
    Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Left:
        return UI::UIFocusNavigationDirection::Left;
    case Platform::Key::Right:
        return UI::UIFocusNavigationDirection::Right;
    case Platform::Key::Up:
        return UI::UIFocusNavigationDirection::Up;
    case Platform::Key::Down:
        return UI::UIFocusNavigationDirection::Down;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIFocusNavigationDirection> focusNavigationDirectionForGamepadButton(
    Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadLeft:
        return UI::UIFocusNavigationDirection::Left;
    case Platform::GamepadButton::DpadRight:
        return UI::UIFocusNavigationDirection::Right;
    case Platform::GamepadButton::DpadUp:
        return UI::UIFocusNavigationDirection::Up;
    case Platform::GamepadButton::DpadDown:
        return UI::UIFocusNavigationDirection::Down;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIRangeInputCommand> rangeInputCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Left:
    case Platform::Key::Down:
        return UI::UIRangeInputCommand::Decrease;
    case Platform::Key::Right:
    case Platform::Key::Up:
        return UI::UIRangeInputCommand::Increase;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIRangeInputCommand>
rangeInputCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadLeft:
    case Platform::GamepadButton::DpadDown:
        return UI::UIRangeInputCommand::Decrease;
    case Platform::GamepadButton::DpadRight:
    case Platform::GamepadButton::DpadUp:
        return UI::UIRangeInputCommand::Increase;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIMenuCommand> menuCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Up:
        return UI::UIMenuCommand::Previous;
    case Platform::Key::Down:
        return UI::UIMenuCommand::Next;
    case Platform::Key::Right:
        return UI::UIMenuCommand::OpenSubmenu;
    case Platform::Key::Left:
        return UI::UIMenuCommand::CloseSubmenu;
    case Platform::Key::Home:
        return UI::UIMenuCommand::First;
    case Platform::Key::End:
        return UI::UIMenuCommand::Last;
    case Platform::Key::Escape:
        return UI::UIMenuCommand::Dismiss;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIMenuCommand>
menuCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadUp:
        return UI::UIMenuCommand::Previous;
    case Platform::GamepadButton::DpadDown:
        return UI::UIMenuCommand::Next;
    case Platform::GamepadButton::DpadRight:
        return UI::UIMenuCommand::OpenSubmenu;
    case Platform::GamepadButton::DpadLeft:
        return UI::UIMenuCommand::CloseSubmenu;
    case Platform::GamepadButton::East:
        return UI::UIMenuCommand::Dismiss;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIDropdownCommand> dropdownCommandForKey(Platform::Key key,
                                                                        bool shiftHeld) noexcept
{
    switch (key)
    {
    case Platform::Key::Up:
        return UI::UIDropdownCommand::PreviousItem;
    case Platform::Key::Down:
        return UI::UIDropdownCommand::NextItem;
    case Platform::Key::Escape:
        return UI::UIDropdownCommand::Dismiss;
    case Platform::Key::Tab:
        return shiftHeld ? UI::UIDropdownCommand::ExitPrevious : UI::UIDropdownCommand::ExitNext;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIDropdownCommand>
dropdownCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadUp:
        return UI::UIDropdownCommand::PreviousItem;
    case Platform::GamepadButton::DpadDown:
        return UI::UIDropdownCommand::NextItem;
    case Platform::GamepadButton::East:
        return UI::UIDropdownCommand::Dismiss;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIListViewCommand> listViewCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Up:
        return UI::UIListViewCommand::PreviousItem;
    case Platform::Key::Down:
        return UI::UIListViewCommand::NextItem;
    case Platform::Key::PageUp:
        return UI::UIListViewCommand::PreviousPage;
    case Platform::Key::PageDown:
        return UI::UIListViewCommand::NextPage;
    case Platform::Key::Home:
        return UI::UIListViewCommand::FirstItem;
    case Platform::Key::End:
        return UI::UIListViewCommand::LastItem;
    case Platform::Key::Enter:
    case Platform::Key::KeypadEnter:
        return UI::UIListViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIVirtualGridViewCommand>
virtualGridViewCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Left:
        return UI::UIVirtualGridViewCommand::PreviousItem;
    case Platform::Key::Right:
        return UI::UIVirtualGridViewCommand::NextItem;
    case Platform::Key::Up:
        return UI::UIVirtualGridViewCommand::PreviousRow;
    case Platform::Key::Down:
        return UI::UIVirtualGridViewCommand::NextRow;
    case Platform::Key::PageUp:
        return UI::UIVirtualGridViewCommand::PreviousPage;
    case Platform::Key::PageDown:
        return UI::UIVirtualGridViewCommand::NextPage;
    case Platform::Key::Home:
        return UI::UIVirtualGridViewCommand::FirstItem;
    case Platform::Key::End:
        return UI::UIVirtualGridViewCommand::LastItem;
    case Platform::Key::Enter:
    case Platform::Key::KeypadEnter:
        return UI::UIVirtualGridViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIVirtualGridViewCommand>
virtualGridViewCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadLeft:
        return UI::UIVirtualGridViewCommand::PreviousItem;
    case Platform::GamepadButton::DpadRight:
        return UI::UIVirtualGridViewCommand::NextItem;
    case Platform::GamepadButton::DpadUp:
        return UI::UIVirtualGridViewCommand::PreviousRow;
    case Platform::GamepadButton::DpadDown:
        return UI::UIVirtualGridViewCommand::NextRow;
    case Platform::GamepadButton::South:
        return UI::UIVirtualGridViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIDataGridCommand>
dataGridCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Left:
        return UI::UIDataGridCommand::PreviousColumn;
    case Platform::Key::Right:
        return UI::UIDataGridCommand::NextColumn;
    case Platform::Key::Up:
        return UI::UIDataGridCommand::PreviousRow;
    case Platform::Key::Down:
        return UI::UIDataGridCommand::NextRow;
    case Platform::Key::PageUp:
        return UI::UIDataGridCommand::PreviousPage;
    case Platform::Key::PageDown:
        return UI::UIDataGridCommand::NextPage;
    case Platform::Key::Home:
        return UI::UIDataGridCommand::FirstCell;
    case Platform::Key::End:
        return UI::UIDataGridCommand::LastCell;
    case Platform::Key::Enter:
    case Platform::Key::KeypadEnter:
        return UI::UIDataGridCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIDataGridCommand>
dataGridCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadLeft:
        return UI::UIDataGridCommand::PreviousColumn;
    case Platform::GamepadButton::DpadRight:
        return UI::UIDataGridCommand::NextColumn;
    case Platform::GamepadButton::DpadUp:
        return UI::UIDataGridCommand::PreviousRow;
    case Platform::GamepadButton::DpadDown:
        return UI::UIDataGridCommand::NextRow;
    case Platform::GamepadButton::South:
        return UI::UIDataGridCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UITreeViewCommand> treeViewCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Up:
        return UI::UITreeViewCommand::PreviousItem;
    case Platform::Key::Down:
        return UI::UITreeViewCommand::NextItem;
    case Platform::Key::PageUp:
        return UI::UITreeViewCommand::PreviousPage;
    case Platform::Key::PageDown:
        return UI::UITreeViewCommand::NextPage;
    case Platform::Key::Home:
        return UI::UITreeViewCommand::FirstItem;
    case Platform::Key::End:
        return UI::UITreeViewCommand::LastItem;
    case Platform::Key::Left:
        return UI::UITreeViewCommand::CollapseOrParent;
    case Platform::Key::Right:
        return UI::UITreeViewCommand::ExpandOrFirstChild;
    case Platform::Key::Space:
        return UI::UITreeViewCommand::ToggleExpanded;
    case Platform::Key::Enter:
    case Platform::Key::KeypadEnter:
        return UI::UITreeViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UIListViewCommand>
listViewCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadUp:
        return UI::UIListViewCommand::PreviousItem;
    case Platform::GamepadButton::DpadDown:
        return UI::UIListViewCommand::NextItem;
    case Platform::GamepadButton::South:
        return UI::UIListViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UITreeViewCommand>
treeViewCommandForGamepadButton(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadUp:
        return UI::UITreeViewCommand::PreviousItem;
    case Platform::GamepadButton::DpadDown:
        return UI::UITreeViewCommand::NextItem;
    case Platform::GamepadButton::DpadLeft:
        return UI::UITreeViewCommand::CollapseOrParent;
    case Platform::GamepadButton::DpadRight:
        return UI::UITreeViewCommand::ExpandOrFirstChild;
    case Platform::GamepadButton::South:
        return UI::UITreeViewCommand::Activate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<UI::UITabViewCommand>
tabViewBoundaryCommandForKey(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Home:
        return UI::UITabViewCommand::First;
    case Platform::Key::End:
        return UI::UITabViewCommand::Last;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool isRepresentableLogicalValue(double value) noexcept
{
    constexpr double MaximumLogicalValue = static_cast<double>((std::numeric_limits<float>::max)());
    return std::isfinite(value) && value >= -MaximumLogicalValue && value <= MaximumLogicalValue;
}

[[nodiscard]] bool pointerTransitionIsValid(const Platform::InputTransitionPayload& payload,
                                            Platform::WindowId primaryWindow) noexcept
{
    if (const auto* value = std::get_if<Platform::PointerButtonTransition>(&payload); value != nullptr)
    {
        return primaryWindow.hasValue() && value->window == primaryWindow &&
               value->pointer == Platform::PrimaryPointerId && isValidPointerButton(value->button) &&
               isValidDigitalTransition(value->state) && isRepresentableLogicalValue(value->logicalX) &&
               isRepresentableLogicalValue(value->logicalY);
    }
    if (const auto* value = std::get_if<Platform::PointerMoveTransition>(&payload); value != nullptr)
    {
        return primaryWindow.hasValue() && value->window == primaryWindow &&
               value->pointer == Platform::PrimaryPointerId && isRepresentableLogicalValue(value->logicalX) &&
               isRepresentableLogicalValue(value->logicalY) && isRepresentableLogicalValue(value->deltaX) &&
               isRepresentableLogicalValue(value->deltaY);
    }
    if (const auto* value = std::get_if<Platform::PointerWheelTransition>(&payload); value != nullptr)
    {
        return primaryWindow.hasValue() && value->window == primaryWindow &&
               value->pointer == Platform::PrimaryPointerId && isRepresentableLogicalValue(value->logicalX) &&
               isRepresentableLogicalValue(value->logicalY) && isRepresentableLogicalValue(value->deltaX) &&
               isRepresentableLogicalValue(value->deltaY);
    }
    return true;
}

[[nodiscard]] std::optional<UI::UIPointerInputEvent>
makePointerInput(Platform::PlatformFrameId frame, usize ordinal, const Platform::InputTransition& transition) noexcept
{
    if (const auto* value = std::get_if<Platform::PointerMoveTransition>(&transition.payload); value != nullptr)
    {
        return UI::UIPointerInputEvent{
            .platformFrame = frame,
            .transitionOrdinal = ordinal,
            .sourceSequence = transition.sequence,
            .window = value->window,
            .pointer = value->pointer,
            .kind = UI::UIRoutedPointerEventKind::Move,
            .position = {.x = static_cast<float>(value->logicalX), .y = static_cast<float>(value->logicalY)},
            .delta = {.x = static_cast<float>(value->deltaX), .y = static_cast<float>(value->deltaY)},
        };
    }
    if (const auto* value = std::get_if<Platform::PointerButtonTransition>(&transition.payload); value != nullptr)
    {
        return UI::UIPointerInputEvent{
            .platformFrame = frame,
            .transitionOrdinal = ordinal,
            .sourceSequence = transition.sequence,
            .window = value->window,
            .pointer = value->pointer,
            .kind = value->state == Platform::DigitalTransition::Down ? UI::UIRoutedPointerEventKind::ButtonDown
                                                                      : UI::UIRoutedPointerEventKind::ButtonUp,
            .position = {.x = static_cast<float>(value->logicalX), .y = static_cast<float>(value->logicalY)},
            .button = value->button,
        };
    }
    if (const auto* value = std::get_if<Platform::PointerWheelTransition>(&transition.payload); value != nullptr)
    {
        return UI::UIPointerInputEvent{
            .platformFrame = frame,
            .transitionOrdinal = ordinal,
            .sourceSequence = transition.sequence,
            .window = value->window,
            .pointer = value->pointer,
            .kind = UI::UIRoutedPointerEventKind::Wheel,
            .position = {.x = static_cast<float>(value->logicalX), .y = static_cast<float>(value->logicalY)},
            .delta = {.x = static_cast<float>(value->deltaX), .y = static_cast<float>(value->deltaY)},
        };
    }
    return std::nullopt;
}

} // namespace

Core::Result<std::unique_ptr<UIInputRouteProducer>>
UIInputRouteProducer::Create(usize rawTransitionCapacity, usize continuousControlClaimCapacity,
                             std::pmr::memory_resource& memoryResource)
{
    if (rawTransitionCapacity == 0 ||
        rawTransitionCapacity > Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "UI Input route raw transition capacity is outside the supported range");
    }
    if (continuousControlClaimCapacity == 0 ||
        continuousControlClaimCapacity > InputActionMapperCapacityConfig::MaximumContinuousControlClaimCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "UI Input route continuous claim capacity is outside the supported range");
    }

    try
    {
        const usize maximumWordCount = consumptionWordCount(rawTransitionCapacity + 1U);
        std::pmr::vector<u64> publishedWords(&memoryResource);
        std::pmr::vector<u64> stagingWords(&memoryResource);
        std::pmr::vector<UI::ContinuousControlClaim> publishedClaims(&memoryResource);
        std::pmr::vector<UI::ContinuousControlClaim> stagingClaims(&memoryResource);
        publishedWords.resize(maximumWordCount, 0);
        stagingWords.resize(maximumWordCount, 0);
        publishedClaims.reserve(continuousControlClaimCapacity);
        stagingClaims.reserve(continuousControlClaimCapacity);
        auto producer = std::unique_ptr<UIInputRouteProducer>(new (std::nothrow) UIInputRouteProducer(
            rawTransitionCapacity, continuousControlClaimCapacity, std::move(publishedWords),
            std::move(stagingWords), std::move(publishedClaims), std::move(stagingClaims)));
        if (producer == nullptr)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "UI Input route producer allocation failed");
        }
        return producer;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "UI Input route storage allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI Input route producer creation failed with an unknown exception");
    }
}

UIInputRouteProducer::UIInputRouteProducer(
    usize rawTransitionCapacity, usize continuousControlClaimCapacity, std::pmr::vector<u64> publishedWords,
    std::pmr::vector<u64> stagingWords, std::pmr::vector<UI::ContinuousControlClaim> publishedClaims,
    std::pmr::vector<UI::ContinuousControlClaim> stagingClaims) noexcept
    : rawTransitionCapacity_(rawTransitionCapacity),
      continuousControlClaimCapacity_(continuousControlClaimCapacity), publishedWords_(std::move(publishedWords)),
      stagingWords_(std::move(stagingWords)), publishedClaims_(std::move(publishedClaims)),
      stagingClaims_(std::move(stagingClaims)), ownerThreadId_(std::this_thread::get_id())
{
}

Core::Status UIInputRouteProducer::preflight(const UI::UIContext* context,
                                             const Platform::PlatformFrameView& platformFrame) const
{
    if (!platformFrame.id().hasValue())
    {
        return invariantFailure("UI Input routing requires a valid Platform frame id");
    }
    if (lastAttemptedPlatformFrame_.has_value() && platformFrame.id().value <= lastAttemptedPlatformFrame_->value)
    {
        return invariantFailure("UI Input route Platform frame ids must be strictly monotonic");
    }

    const std::span<const Platform::WindowFrameSnapshot> windows = platformFrame.windows();
    if (windows.size() > 1U)
    {
        return invariantFailure("UI Input routing accepts at most one primary Window snapshot");
    }
    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (primaryWindow != nullptr &&
        (!primaryWindow->metrics.window.hasValue() || primaryWindow->metrics.window != primaryWindow->input.window))
    {
        return invariantFailure("UI Input routing received an inconsistent primary Window owner");
    }
    if (context != nullptr && (primaryWindow == nullptr || context->ownerWindow() != primaryWindow->metrics.window))
    {
        return invariantFailure("UI Context and Platform frame must have the same primary owner Window");
    }

    const Platform::InputTransitionBatch transitions = platformFrame.inputTransitions();
    if (transitions.size() > rawTransitionCapacity_ + 1U)
    {
        return invariantFailure("UI Input transition batch exceeds configured capacity");
    }
    if (transitions.size() > rawTransitionCapacity_ &&
        !std::holds_alternative<Platform::InputStreamReset>(transitions.back().payload))
    {
        return invariantFailure("UI Input reserved transition slot is not an InputStreamReset");
    }
    if (!transitions.empty() && lastAttemptedRawSequence_.has_value() &&
        transitions.front().sequence <= *lastAttemptedRawSequence_)
    {
        return invariantFailure("UI Input transition sequence regressed across Platform frames");
    }

    u64 previousSequence = 0;
    bool sawReset = false;
    for (const Platform::InputTransition& transition : transitions)
    {
        if (transition.sequence == 0 || transition.sequence <= previousSequence)
        {
            return invariantFailure("UI Input transition sequence must be strictly monotonic");
        }
        if (sawReset)
        {
            return invariantFailure("UI Input transitions cannot follow InputStreamReset");
        }
        const Platform::WindowId primaryWindowId =
            primaryWindow == nullptr ? Platform::WindowId{} : primaryWindow->metrics.window;
        if (!pointerTransitionIsValid(transition.payload, primaryWindowId))
        {
            return invariantFailure("UI pointer transition identity, state, owner, position, or delta is invalid");
        }

        sawReset = std::holds_alternative<Platform::InputStreamReset>(transition.payload);
        previousSequence = transition.sequence;
    }
    return Core::success();
}

Core::Result<UIInputRouteOutputView> UIInputRouteProducer::produce(UI::UIContext* context,
                                                                   const Platform::PlatformFrameView& platformFrame)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "UI Input route producer was accessed from a non-owner thread");
    }
    if (producing_)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "UI Input route production is already in progress");
    }

    producing_ = true;
    auto producingCleanup = Core::makeScopeExit([this]() noexcept { producing_ = false; });
    if (Core::Status validation = preflight(context, platformFrame); !validation)
    {
        return Core::failure(std::move(validation.error()));
    }

    const Platform::InputTransitionBatch transitions = platformFrame.inputTransitions();
    lastAttemptedPlatformFrame_ = platformFrame.id();
    if (!transitions.empty())
    {
        lastAttemptedRawSequence_ = transitions.back().sequence;
    }

    if (context == nullptr)
    {
        return UIInputRouteOutputView{
            .consumption = UI::InputTransitionConsumptionView::None(platformFrame.id(), transitions.size()),
            .claims = UI::ContinuousControlClaimsView::None(platformFrame.id()),
        };
    }

    const usize requiredWordCount = consumptionWordCount(transitions.size());
    std::fill_n(stagingWords_.begin(), requiredWordCount, u64{0});
    stagingClaims_.clear();
    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    bool anyConsumed = false;
    for (usize ordinal = 0; ordinal < transitions.size(); ++ordinal)
    {
        auto observation =
            flowInputDeviceObservation(transitions[ordinal].payload, *context);
        if (!observation)
        {
            Core::Error error = std::move(observation.error());
            error.addContext("UIInputRouteProducer::produce(resolve-flow-input-device)");
            return Core::failure(std::move(error));
        }
        if (observation->has_value())
        {
            Core::Status observed = context->observeFlowInputDevice(
                platformFrame.id(), transitions[ordinal].sequence,
                (**observation).localUser, (**observation).device,
                (**observation).gamepad);
            if (!observed)
            {
                Core::Error error = std::move(observed.error());
                error.addContext("UIInputRouteProducer::produce(flow-input-device)");
                return Core::failure(std::move(error));
            }
        }
        if (const auto* cancel =
                std::get_if<Platform::InputCancelTransition>(
                    &transitions[ordinal].payload);
            cancel != nullptr) {
            if (!cancel->gamepad.has_value()
                && cancel->routedWindow == context->ownerWindow()) {
                Core::Status cancelStatus =
                    context->cancelPointerInteraction(cancel->routedWindow);
                if (!cancelStatus) {
                    Core::Error error = std::move(cancelStatus.error());
                    error.addContext("UIInputRouteProducer::produce(cancel)");
                    return Core::failure(std::move(error));
                }
            } else if (cancel->gamepad.has_value()
                       && cancel->routedWindow == context->ownerWindow()) {
                auto localUser =
                    context->flowLocalUserForGamepad(*cancel->gamepad);
                if (!localUser)
                {
                    Core::Error error = std::move(localUser.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(cancel-flow-local-user)");
                    return Core::failure(std::move(error));
                }
                auto inputDevice = context->flowInputDeviceState(*localUser);
                if (!inputDevice)
                {
                    Core::Error error = std::move(inputDevice.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(cancel-flow-input-device)");
                    return Core::failure(std::move(error));
                }
                if (inputDevice->device == UI::UIFlowInputDevice::Gamepad &&
                    inputDevice->gamepad == cancel->gamepad)
                {
                    Core::Status observed = context->observeFlowInputDevice(
                        platformFrame.id(), transitions[ordinal].sequence,
                        *localUser, UI::UIFlowInputDevice::KeyboardMouse);
                    if (!observed)
                    {
                        Core::Error error = std::move(observed.error());
                        error.addContext(
                            "UIInputRouteProducer::produce(cancel-flow-input-device-fallback)");
                        return Core::failure(std::move(error));
                    }
                }
                Core::Status cancelStatus =
                    context->cancelDefaultActionInteraction(
                        cancel->routedWindow,
                        cancel->gamepad);
                if (!cancelStatus) {
                    Core::Error error = std::move(cancelStatus.error());
                    error.addContext("UIInputRouteProducer::produce(cancel-default-action)");
                    return Core::failure(std::move(error));
                }
            }
            continue;
        }
        if (const auto* reset =
                std::get_if<Platform::InputStreamReset>(
                    &transitions[ordinal].payload);
            reset != nullptr) {
            if (!reset->routedWindow.has_value()
                || *reset->routedWindow == context->ownerWindow()) {
                for (usize userIndex = 0;
                     userIndex < UI::UIFlowLocalUserCapacity; ++userIndex)
                {
                    const UI::UIFlowLocalUserId localUser{
                        static_cast<u32>(userIndex + 1U)};
                    auto inputDevice = context->flowInputDeviceState(localUser);
                    if (!inputDevice)
                    {
                        Core::Error error = std::move(inputDevice.error());
                        error.addContext(
                            "UIInputRouteProducer::produce(reset-flow-input-device)");
                        return Core::failure(std::move(error));
                    }
                    if (inputDevice->device != UI::UIFlowInputDevice::Gamepad)
                    {
                        continue;
                    }
                    Core::Status observed = context->observeFlowInputDevice(
                        platformFrame.id(), transitions[ordinal].sequence,
                        localUser, UI::UIFlowInputDevice::KeyboardMouse);
                    if (!observed)
                    {
                        Core::Error error = std::move(observed.error());
                        error.addContext(
                            "UIInputRouteProducer::produce(reset-flow-input-device-fallback)");
                        return Core::failure(std::move(error));
                    }
                }
                Core::Status cancelStatus =
                    context->cancelPointerInteraction(context->ownerWindow());
                if (!cancelStatus) {
                    Core::Error error = std::move(cancelStatus.error());
                    error.addContext("UIInputRouteProducer::produce(reset)");
                    return Core::failure(std::move(error));
                }
                cancelStatus = context->cancelDefaultActionInteraction(
                    context->ownerWindow());
                if (!cancelStatus)
                {
                    Core::Error error = std::move(cancelStatus.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(reset-default-action)");
                    return Core::failure(std::move(error));
                }
            }
            continue;
        }

        // IME composition / commit targeting the Label under ime focus.
        if (const auto* composition =
                std::get_if<Platform::TextCompositionTransition>(
                    &transitions[ordinal].payload);
            composition != nullptr
            && composition->window == context->ownerWindow())
        {
            auto routed = context->routeTextComposition(
                composition->window,
                platformFrame.id(),
                transitions[ordinal].sequence,
                composition->preeditUtf8,
                composition->cursorCodepoint,
                composition->stage);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(text-composition)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
            }
            continue;
        }
        if (const auto* text =
                std::get_if<Platform::TextInputTransition>(
                    &transitions[ordinal].payload);
            text != nullptr
            && text->window == context->ownerWindow())
        {
            auto routed = context->routeTextInput(
                text->window,
                platformFrame.id(),
                transitions[ordinal].sequence,
                text->committedUtf8);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(text-input)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
            }
            continue;
        }

        // Context Menu and Shift+F10 open the Menu associated with the focused
        // node (or its nearest ancestor). Route F10 Up even if Shift was
        // released first so a claimed physical pair cannot leak to gameplay.
        if (const auto* key = std::get_if<Platform::KeyTransition>(
                &transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            const bool pressed = key->state == Platform::DigitalTransition::Down;
            std::optional<UI::UIMenuInvocationCommand> command;
            if (key->key == Platform::Key::Menu)
            {
                command = UI::UIMenuInvocationCommand::ContextMenuKey;
            } else if (key->key == Platform::Key::F10)
            {
                const bool shiftHeld = primaryWindow != nullptr &&
                    (primaryWindow->input.isHeld(Platform::Key::LeftShift) ||
                     primaryWindow->input.isHeld(Platform::Key::RightShift));
                if (!pressed || shiftHeld)
                {
                    command = UI::UIMenuInvocationCommand::ShiftF10;
                }
            }
            if (command.has_value())
            {
                auto routed = context->routeMenuInvocation(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(menu-invocation)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // A Menu owns its directional and dismissal commands before every other
        // focused control. Matching releases remain consumed after dismissal.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            if (const auto command = menuCommandForKey(key->key); command.has_value())
            {
                auto routed = context->routeMenuCommand(
                    *command, key->state == Platform::DigitalTransition::Down);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(menu-key-command)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            if (const auto command = menuCommandForGamepadButton(gamepad->button); command.has_value())
            {
                auto routed = context->routeMenuCommand(
                    *command, gamepad->state == Platform::DigitalTransition::Down);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(menu-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // An open Dropdown owns navigation/cancel/Tab before general text-edit
        // commands and focus traversal. Matching releases remain consumed after
        // dismissal so gameplay never observes half of a digital transition.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            bool shiftHeld = false;
            if (primaryWindow != nullptr)
            {
                shiftHeld = primaryWindow->input.isHeld(Platform::Key::LeftShift) ||
                            primaryWindow->input.isHeld(Platform::Key::RightShift);
            }
            if (key->key == Platform::Key::Tab && key->state == Platform::DigitalTransition::Up)
            {
                auto previousRelease =
                    context->routeDropdownCommand(UI::UIDropdownCommand::ExitPrevious, false);
                if (!previousRelease)
                {
                    Core::Error error = std::move(previousRelease.error());
                    error.addContext("UIInputRouteProducer::produce(dropdown-tab-release)");
                    return Core::failure(std::move(error));
                }
                auto nextRelease = context->routeDropdownCommand(UI::UIDropdownCommand::ExitNext, false);
                if (!nextRelease)
                {
                    Core::Error error = std::move(nextRelease.error());
                    error.addContext("UIInputRouteProducer::produce(dropdown-tab-release)");
                    return Core::failure(std::move(error));
                }
                if (previousRelease->consumed || nextRelease->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
            if (const auto command = dropdownCommandForKey(key->key, shiftHeld); command.has_value())
            {
                auto routed = context->routeDropdownCommand(
                    *command, key->state == Platform::DigitalTransition::Down);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(dropdown-key-command)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            if (const auto command = dropdownCommandForGamepadButton(gamepad->button); command.has_value())
            {
                auto routed = context->routeDropdownCommand(
                    *command, gamepad->state == Platform::DigitalTransition::Down);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(dropdown-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // An open Menu or Dropdown gets first refusal above. Otherwise Escape and
        // Gamepad East route Back only to the topmost committed active Flow
        // Screen. A handled Down claims its matching Up across Screen changes.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow() &&
            key->key == Platform::Key::Escape)
        {
            const Platform::DigitalControlIdentity control = Platform::KeyControlIdentity{
                .window = key->window,
                .key = key->key,
            };
            auto routed = context->routeFlowAction(
                platformFrame.id(), transitions[ordinal].sequence,
                UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Back,
                UI::UIFlowActionSource::Keyboard,
                key->state == Platform::DigitalTransition::Down, control);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(keyboard-flow-back)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow() &&
            gamepad->button == Platform::GamepadButton::East)
        {
            const Platform::DigitalControlIdentity control =
                Platform::GamepadButtonControlIdentity{
                    .routedWindow = gamepad->routedWindow,
                    .gamepad = gamepad->gamepad,
                    .button = gamepad->button,
                };
            auto routed = routeGamepadFlowAction(
                *context, platformFrame.id(), transitions[ordinal].sequence,
                UI::UIFlowAction::Back, *gamepad, control);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(gamepad-flow-back)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
        }

        // Menu is a product-level Flow action rather than a focused-control
        // default action. Printable P yields to an active TextEdit on Down, but
        // an already-claimed Up is routed first so focus changes cannot leak the
        // second half of the physical input pair to gameplay. Gamepad Start has
        // no text-entry meaning and routes directly.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow() &&
            key->key == Platform::Key::P)
        {
            const bool pressed = key->state == Platform::DigitalTransition::Down;
            if (!pressed || !context->imeFocus().hasValue())
            {
                const Platform::DigitalControlIdentity control = Platform::KeyControlIdentity{
                    .window = key->window,
                    .key = key->key,
                };
                auto routed = context->routeFlowAction(
                    platformFrame.id(), transitions[ordinal].sequence,
                    UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Menu,
                    UI::UIFlowActionSource::Keyboard, pressed, control);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(keyboard-flow-menu)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow() &&
            gamepad->button == Platform::GamepadButton::Start)
        {
            const Platform::DigitalControlIdentity control =
                Platform::GamepadButtonControlIdentity{
                    .routedWindow = gamepad->routedWindow,
                    .gamepad = gamepad->gamepad,
                    .button = gamepad->button,
                };
            auto routed = routeGamepadFlowAction(
                *context, platformFrame.id(), transitions[ordinal].sequence,
                UI::UIFlowAction::Menu, *gamepad, control);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(gamepad-flow-menu)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
        }

        // Focused collection controls own their navigation and activation keys
        // before TextEdit/focus traversal/default Accept. Route matching Up to
        // every collection command state machine so a focus change cannot strand a pressed
        // command or expose half of a digital transition to gameplay.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            const bool pressed = key->state == Platform::DigitalTransition::Down;
            bool consumed = false;
            if (const auto command = treeViewCommandForKey(key->key); command.has_value())
            {
                auto routed = context->routeTreeViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(tree-view-key-command)");
                    return Core::failure(std::move(error));
                }
                consumed = routed->consumed;
            }
            if (const auto command = listViewCommandForKey(key->key); command.has_value())
            {
                auto routed = context->routeListViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(list-view-key-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (const auto command = virtualGridViewCommandForKey(key->key); command.has_value())
            {
                auto routed = context->routeVirtualGridViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(virtual-grid-key-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (const auto command = dataGridCommandForKey(key->key);
                command.has_value())
            {
                auto routed = context->routeDataGridCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(data-grid-key-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            const bool pressed = gamepad->state == Platform::DigitalTransition::Down;
            bool consumed = false;
            if (const auto command = treeViewCommandForGamepadButton(gamepad->button); command.has_value())
            {
                auto routed = context->routeTreeViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(tree-view-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                consumed = routed->consumed;
            }
            if (const auto command = listViewCommandForGamepadButton(gamepad->button); command.has_value())
            {
                auto routed = context->routeListViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(list-view-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (const auto command = virtualGridViewCommandForGamepadButton(gamepad->button); command.has_value())
            {
                auto routed = context->routeVirtualGridViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(virtual-grid-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (const auto command =
                    dataGridCommandForGamepadButton(gamepad->button);
                command.has_value())
            {
                auto routed = context->routeDataGridCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext(
                        "UIInputRouteProducer::produce(data-grid-gamepad-command)");
                    return Core::failure(std::move(error));
                }
                consumed = consumed || routed->consumed;
            }
            if (consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
        }

        // A focused Tab owns the axis matching its TabView placement before
        // generic spatial focus. Home/End use the same fixed-capacity command
        // path and claim their matching release.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            const bool pressed = key->state == Platform::DigitalTransition::Down;
            if (const auto direction = focusNavigationDirectionForKey(key->key); direction.has_value())
            {
                auto routed = context->routeFocusedTabViewDirection(*direction, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(tab-view-key-direction)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
            if (const auto command = tabViewBoundaryCommandForKey(key->key); command.has_value())
            {
                auto routed = context->routeFocusedTabViewCommand(*command, pressed);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(tab-view-key-boundary)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            if (const auto direction = focusNavigationDirectionForGamepadButton(gamepad->button);
                direction.has_value())
            {
                auto routed = context->routeFocusedTabViewDirection(
                    *direction, gamepad->state == Platform::DigitalTransition::Down);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(tab-view-gamepad-direction)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // Focused RangeInput adjustment owns Arrow/D-pad after open dropdowns
        // and collection controls, but before generic spatial focus. The
        // capability route declines without latching when no value changes, so
        // disabled/read-only/edge input remains visible to gameplay.
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            if (const auto command = rangeInputCommandForKey(key->key); command.has_value())
            {
                const Platform::DigitalControlIdentity control = Platform::KeyControlIdentity{
                    .window = key->window,
                    .key = key->key,
                };
                auto routed = context->routeRangeInputCommand(
                    platformFrame.id(), transitions[ordinal].sequence, *command,
                    key->state == Platform::DigitalTransition::Down, control);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(keyboard-range-input)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
                if (routed->targeted)
                {
                    continue;
                }
            }
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            if (const auto command = rangeInputCommandForGamepadButton(gamepad->button); command.has_value())
            {
                const Platform::DigitalControlIdentity control = Platform::GamepadButtonControlIdentity{
                    .routedWindow = gamepad->routedWindow,
                    .gamepad = gamepad->gamepad,
                    .button = gamepad->button,
                };
                auto routed = context->routeRangeInputCommand(
                    platformFrame.id(), transitions[ordinal].sequence, *command,
                    gamepad->state == Platform::DigitalTransition::Down, control);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(gamepad-range-input)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
                if (routed->targeted)
                {
                    continue;
                }
            }
        }

        // Generic spatial focus runs only after controls with directional state
        // (Dropdown/ListView/TreeView/TextEdit/RangeInput) have declined the transition.
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr && gamepad->routedWindow == context->ownerWindow())
        {
            if (const auto direction = focusNavigationDirectionForGamepadButton(gamepad->button);
                direction.has_value())
            {
                auto routed = context->routeFocusNavigation(
                    *direction, gamepad->state == Platform::DigitalTransition::Down,
                    UI::UIInputModality::Gamepad);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(gamepad-focus-navigation)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // A focused TextEdit owns keyboard transitions so physical gameplay
        // bindings cannot fire while text/IME input is active. TextInput still
        // carries printable UTF-8; key commands only mutate caret/selection.
        if (const auto* key =
            std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr
            && key->window == context->ownerWindow()
            && key->key != Platform::Key::Tab
            // Enter/Escape are editor-level commit/cancel commands. Keep
            // them available to the frame action mapper while a TextEdit is
            // focused; printable and caret-editing keys remain owned here.
            && key->key != Platform::Key::Enter
            && key->key != Platform::Key::KeypadEnter
            && key->key != Platform::Key::Escape
            && context->imeFocus().hasValue())
        {
            if (key->state == Platform::DigitalTransition::Down)
            {
                bool shiftHeld = false;
                bool controlHeld = false;
                if (primaryWindow != nullptr)
                {
                    shiftHeld = primaryWindow->input.isHeld(Platform::Key::LeftShift)
                        || primaryWindow->input.isHeld(Platform::Key::RightShift);
                    controlHeld = primaryWindow->input.isHeld(Platform::Key::LeftControl)
                        || primaryWindow->input.isHeld(Platform::Key::RightControl);
                }
                if (const auto command = textEditCommandForKey(key->key, controlHeld);
                    command.has_value())
                {
                    auto routed = context->routeTextEditCommand(
                        key->window,
                        platformFrame.id(),
                        transitions[ordinal].sequence,
                        *command,
                        shiftHeld);
                    if (!routed)
                    {
                        Core::Error error = std::move(routed.error());
                        error.addContext("UIInputRouteProducer::produce(text-edit-command)");
                        return Core::failure(std::move(error));
                    }
                }
            }
            stagingWords_[ordinal / BitsPerConsumptionWord] |=
                u64{1} << (ordinal % BitsPerConsumptionWord);
            anyConsumed = true;
            continue;
        }

        if (const auto* key =
                std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr && key->window == context->ownerWindow())
        {
            if (const auto direction = focusNavigationDirectionForKey(key->key); direction.has_value())
            {
                auto routed =
                    context->routeFocusNavigation(*direction,
                                                   key->state == Platform::DigitalTransition::Down,
                                                   UI::UIInputModality::Keyboard);
                if (!routed)
                {
                    Core::Error error = std::move(routed.error());
                    error.addContext("UIInputRouteProducer::produce(keyboard-focus-navigation)");
                    return Core::failure(std::move(error));
                }
                if (routed->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }
        }

        // Tab cycles keyboard focus. Shift is read from the
        // primary-window held key snapshot (KeyTransition carries no modifiers).
        if (const auto* key =
                std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr
            && key->window == context->ownerWindow()
            && key->key == Platform::Key::Tab)
        {
            bool consumed = context->defaultActionFocus().hasValue();
            if (key->state == Platform::DigitalTransition::Down) {
                bool reverse = false;
                if (primaryWindow != nullptr)
                {
                    reverse = primaryWindow->input.isHeld(Platform::Key::LeftShift)
                        || primaryWindow->input.isHeld(Platform::Key::RightShift);
                }
                auto step = context->routeDefaultActionFocusStep(reverse);
                if (!step)
                {
                    Core::Error error = std::move(step.error());
                    error.addContext("UIInputRouteProducer::produce(tab-focus)");
                    return Core::failure(std::move(error));
                }
                consumed = step->consumed;
            }
            if (consumed) {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
            }
            continue;
        }

        // Focused controls own Accept first. Otherwise Enter/Keypad Enter and
        // Gamepad South route Confirm to the topmost active Flow Screen. A Flow
        // release is checked first so a Screen pop cannot strand its press latch.
        if (const auto* key =
                std::get_if<Platform::KeyTransition>(&transitions[ordinal].payload);
            key != nullptr
            && key->window == context->ownerWindow()
            && (key->key == Platform::Key::Enter
                || key->key == Platform::Key::Space
                || key->key == Platform::Key::KeypadEnter))
        {
            const Platform::DigitalControlIdentity control =
                Platform::KeyControlIdentity{
                    .window = key->window,
                    .key = key->key,
                };
            const bool pressed = key->state == Platform::DigitalTransition::Down;
            const bool supportsFlowConfirm = key->key != Platform::Key::Space;
            if (context->imeFocus().hasValue())
            {
                // A focused TextEdit owns neither confirmation nor cancel;
                // leave these physical transitions unconsumed so the product
                // action map can implement context-specific commands.
                continue;
            }
            if (!pressed && supportsFlowConfirm)
            {
                auto flowRelease = context->routeFlowAction(
                    platformFrame.id(), transitions[ordinal].sequence,
                    UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Confirm,
                    UI::UIFlowActionSource::Keyboard, false, control);
                if (!flowRelease)
                {
                    Core::Error error = std::move(flowRelease.error());
                    error.addContext("UIInputRouteProducer::produce(keyboard-flow-confirm-release)");
                    return Core::failure(std::move(error));
                }
                if (flowRelease->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }

            auto routed = pressed
                ? context->routeDefaultActionActivate(
                    platformFrame.id(),
                    transitions[ordinal].sequence,
                    UI::UIButtonActivationSource::Keyboard,
                    control)
                : context->routeDefaultActionRelease(
                    platformFrame.id(),
                    transitions[ordinal].sequence,
                    UI::UIButtonActivationSource::Keyboard,
                    control);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(keyboard-accept)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
            if (pressed && supportsFlowConfirm)
            {
                auto flow = context->routeFlowAction(
                    platformFrame.id(), transitions[ordinal].sequence,
                    UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Confirm,
                    UI::UIFlowActionSource::Keyboard, true, control);
                if (!flow)
                {
                    Core::Error error = std::move(flow.error());
                    error.addContext("UIInputRouteProducer::produce(keyboard-flow-confirm)");
                    return Core::failure(std::move(error));
                }
                if (flow->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                }
            }
            continue;
        }
        if (const auto* gamepad =
                std::get_if<Platform::GamepadButtonTransition>(&transitions[ordinal].payload);
            gamepad != nullptr
            && gamepad->routedWindow == context->ownerWindow()
            && gamepad->button == Platform::GamepadButton::South)
        {
            const Platform::DigitalControlIdentity control =
                Platform::GamepadButtonControlIdentity{
                    .routedWindow = gamepad->routedWindow,
                    .gamepad = gamepad->gamepad,
                    .button = gamepad->button,
                };
            const bool pressed = gamepad->state == Platform::DigitalTransition::Down;
            if (!pressed)
            {
                auto flowRelease = routeGamepadFlowAction(
                    *context, platformFrame.id(), transitions[ordinal].sequence,
                    UI::UIFlowAction::Confirm, *gamepad, control);
                if (!flowRelease)
                {
                    Core::Error error = std::move(flowRelease.error());
                    error.addContext("UIInputRouteProducer::produce(gamepad-flow-confirm-release)");
                    return Core::failure(std::move(error));
                }
                if (flowRelease->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                    continue;
                }
            }

            auto routed = pressed
                ? context->routeDefaultActionActivate(
                    platformFrame.id(),
                    transitions[ordinal].sequence,
                    UI::UIButtonActivationSource::Gamepad,
                    control)
                : context->routeDefaultActionRelease(
                    platformFrame.id(),
                    transitions[ordinal].sequence,
                    UI::UIButtonActivationSource::Gamepad,
                    control);
            if (!routed)
            {
                Core::Error error = std::move(routed.error());
                error.addContext("UIInputRouteProducer::produce(gamepad-accept)");
                return Core::failure(std::move(error));
            }
            if (routed->consumed)
            {
                stagingWords_[ordinal / BitsPerConsumptionWord] |=
                    u64{1} << (ordinal % BitsPerConsumptionWord);
                anyConsumed = true;
                continue;
            }
            if (pressed)
            {
                auto flow = routeGamepadFlowAction(
                    *context, platformFrame.id(), transitions[ordinal].sequence,
                    UI::UIFlowAction::Confirm, *gamepad, control);
                if (!flow)
                {
                    Core::Error error = std::move(flow.error());
                    error.addContext("UIInputRouteProducer::produce(gamepad-flow-confirm)");
                    return Core::failure(std::move(error));
                }
                if (flow->consumed)
                {
                    stagingWords_[ordinal / BitsPerConsumptionWord] |=
                        u64{1} << (ordinal % BitsPerConsumptionWord);
                    anyConsumed = true;
                }
            }
            continue;
        }

        const std::optional<UI::UIPointerInputEvent> input =
            makePointerInput(platformFrame.id(), ordinal, transitions[ordinal]);
        if (!input.has_value())
        {
            continue;
        }

        auto routeResult = context->routePointerInput(*input);
        if (!routeResult)
        {
            Core::Error error = std::move(routeResult.error());
            error.addContext("UIInputRouteProducer::produce");
            return Core::failure(std::move(error));
        }
        if (routeResult->consumed)
        {
            stagingWords_[ordinal / BitsPerConsumptionWord] |= u64{1} << (ordinal % BitsPerConsumptionWord);
            anyConsumed = true;
        }

        if (primaryWindow == nullptr)
        {
            continue;
        }
        for (usize buttonIndex = 0; buttonIndex < Platform::PointerButtonCount; ++buttonIndex)
        {
            if (!routeResult->claimedPointerButtons.test(buttonIndex) ||
                !primaryWindow->input.pointer.heldButtons.test(buttonIndex))
            {
                continue;
            }
            const Platform::PointerButtonControlIdentity control{
                .window = input->window,
                .pointer = input->pointer,
                .button = static_cast<Platform::PointerButton>(buttonIndex),
            };
            const auto existing = std::ranges::find_if(
                stagingClaims_, [&control](const UI::ContinuousControlClaim& claim) noexcept {
                    const auto* claimed = std::get_if<Platform::PointerButtonControlIdentity>(&claim.control);
                    return claimed != nullptr && *claimed == control;
                });
            if (existing != stagingClaims_.end())
            {
                continue;
            }
            if (stagingClaims_.size() >= continuousControlClaimCapacity_)
            {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "UI continuous control claim capacity has been exhausted");
            }
            stagingClaims_.push_back(UI::ContinuousControlClaim{.control = control});
        }
    }

    const bool anyClaims = !stagingClaims_.empty();
    if (!anyConsumed && !anyClaims)
    {
        return UIInputRouteOutputView{
            .consumption = UI::InputTransitionConsumptionView::None(platformFrame.id(), transitions.size()),
            .claims = UI::ContinuousControlClaimsView::None(platformFrame.id()),
        };
    }

    if (anyConsumed)
    {
        publishedWords_.swap(stagingWords_);
    }
    if (anyClaims)
    {
        publishedClaims_.swap(stagingClaims_);
    }
    return UIInputRouteOutputView{
        .consumption = anyConsumed
            ? UI::InputTransitionConsumptionView{
                  .platformFrame = platformFrame.id(),
                  .transitionCount = transitions.size(),
                  .consumedOrdinalWords = std::span<const u64>(publishedWords_.data(), requiredWordCount),
              }
            : UI::InputTransitionConsumptionView::None(platformFrame.id(), transitions.size()),
        .claims = anyClaims
            ? UI::ContinuousControlClaimsView{
                  .platformFrame = platformFrame.id(),
                  .controls = std::span<const UI::ContinuousControlClaim>(publishedClaims_),
              }
            : UI::ContinuousControlClaimsView::None(platformFrame.id()),
    };
}

} // namespace Tina::Runtime::Input
