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

[[nodiscard]] Core::Status invariantFailure(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation, message);
}

[[nodiscard]] constexpr usize consumptionWordCount(usize transitionCount) noexcept
{
    return (transitionCount + BitsPerConsumptionWord - 1U) / BitsPerConsumptionWord;
}

[[nodiscard]] bool isValidDigitalTransition(Platform::DigitalTransition state) noexcept
{
    return state == Platform::DigitalTransition::Down || state == Platform::DigitalTransition::Up;
}

[[nodiscard]] bool isValidPointerButton(Platform::PointerButton button) noexcept
{
    return button < Platform::PointerButton::Count;
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
            }
            continue;
        }
        if (const auto* reset =
                std::get_if<Platform::InputStreamReset>(
                    &transitions[ordinal].payload);
            reset != nullptr) {
            if (!reset->routedWindow.has_value()
                || *reset->routedWindow == context->ownerWindow()) {
                Core::Status cancelStatus =
                    context->cancelPointerInteraction(context->ownerWindow());
                if (!cancelStatus) {
                    Core::Error error = std::move(cancelStatus.error());
                    error.addContext("UIInputRouteProducer::produce(reset)");
                    return Core::failure(std::move(error));
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
