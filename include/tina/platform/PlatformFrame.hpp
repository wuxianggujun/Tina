#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/Window.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace Tina::Platform {

struct PlatformFrameId final {
    u64 value = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return value != 0;
    }
    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }
    auto operator<=>(const PlatformFrameId&) const = default;
};

struct WindowFrameSnapshot final {
    WindowMetricsSnapshot metrics{};
    WindowInputSnapshot input{};
};

struct WindowMetricsChangedEvent final {
    WindowId window{};
    u64 metricsRevision = 0;
};

struct GamepadConnectedEvent final {
    GamepadId gamepad{};
};

struct GamepadDisconnectedEvent final {
    GamepadId gamepad{};
};

enum class PlatformEventResetReason : u8 {
    CapacityExceeded,
    BackendRecovery,
};

struct PlatformEventStreamReset final {
    PlatformEventResetReason reason = PlatformEventResetReason::CapacityExceeded;
};

using PlatformEventPayload =
    std::variant<WindowMetricsChangedEvent, GamepadConnectedEvent, GamepadDisconnectedEvent, PlatformEventStreamReset>;

struct PlatformEvent final {
    u64 sequence = 0;
    PlatformEventPayload payload{};
};

using PlatformEventBatch = std::span<const PlatformEvent>;

struct PlatformFrameDiagnostics final {
    u32 inputOverflowCount = 0;
    u32 inputTextOverflowCount = 0;
    u32 platformEventOverflowCount = 0;
};

class PlatformFrameBuilder;

// Borrowed immutable view. Every span and string_view becomes invalid when the
// backend begins its next poll, and must not escape Runtime's Poll/Input phase.
class PlatformFrameView final {
  public:
    [[nodiscard]] PlatformFrameId id() const noexcept
    {
        return id_;
    }
    [[nodiscard]] std::span<const WindowFrameSnapshot> windows() const noexcept
    {
        return windows_;
    }
    [[nodiscard]] const WindowFrameSnapshot* primaryWindow() const noexcept
    {
        return windows_.empty() ? nullptr : &windows_.front();
    }
    [[nodiscard]] std::span<const GamepadSnapshot> gamepads() const noexcept
    {
        return gamepads_;
    }
    [[nodiscard]] InputTransitionBatch inputTransitions() const noexcept
    {
        return inputTransitions_;
    }
    [[nodiscard]] PlatformEventBatch platformEvents() const noexcept
    {
        return platformEvents_;
    }
    [[nodiscard]] const PlatformFrameDiagnostics& diagnostics() const noexcept
    {
        return diagnostics_;
    }

  private:
    friend class PlatformFrameBuilder;

    PlatformFrameView(PlatformFrameId id, std::span<const WindowFrameSnapshot> windows,
                      std::span<const GamepadSnapshot> gamepads, std::span<const InputTransition> inputTransitions,
                      std::span<const PlatformEvent> platformEvents, PlatformFrameDiagnostics diagnostics) noexcept
        : id_(id), windows_(windows), gamepads_(gamepads), inputTransitions_(inputTransitions),
          platformEvents_(platformEvents), diagnostics_(diagnostics)
    {
    }

    PlatformFrameId id_{};
    std::span<const WindowFrameSnapshot> windows_{};
    std::span<const GamepadSnapshot> gamepads_{};
    std::span<const InputTransition> inputTransitions_{};
    std::span<const PlatformEvent> platformEvents_{};
    PlatformFrameDiagnostics diagnostics_{};
};

class PlatformPollResult final {
  public:
    struct ContinueFrame final {
        PlatformFrameView frame;
    };

    struct ExitRequested final {};

    using Outcome = std::variant<ContinueFrame, ExitRequested>;

    [[nodiscard]] static PlatformPollResult Continue(PlatformFrameView frame) noexcept
    {
        return PlatformPollResult(ContinueFrame{frame});
    }

    [[nodiscard]] static PlatformPollResult Exit() noexcept
    {
        return PlatformPollResult(ExitRequested{});
    }

    [[nodiscard]] bool isContinueFrame() const noexcept
    {
        return std::holds_alternative<ContinueFrame>(outcome_);
    }

    [[nodiscard]] bool isExitRequested() const noexcept
    {
        return std::holds_alternative<ExitRequested>(outcome_);
    }

    [[nodiscard]] const PlatformFrameView* frame() const noexcept
    {
        const auto* continuation = std::get_if<ContinueFrame>(&outcome_);
        return continuation == nullptr ? nullptr : &continuation->frame;
    }

    [[nodiscard]] const Outcome& outcome() const noexcept
    {
        return outcome_;
    }

  private:
    explicit PlatformPollResult(ContinueFrame outcome) noexcept : outcome_(outcome)
    {
    }

    explicit PlatformPollResult(ExitRequested outcome) noexcept : outcome_(outcome)
    {
    }

    Outcome outcome_;
};

struct PlatformFrameCapacityConfig final {
    static constexpr u32 DefaultInputTransitionCapacity = 256;
    static constexpr u32 MaximumInputTransitionCapacity = 4096;
    static constexpr u32 DefaultInputTextByteCapacity = 16U * 1024U;
    static constexpr u32 MaximumInputTextByteCapacity = 1024U * 1024U;
    static constexpr u32 DefaultPlatformEventCapacity = 64;
    static constexpr u32 MaximumPlatformEventCapacity = 1024;

    u32 inputTransitionCapacity = DefaultInputTransitionCapacity;
    u32 inputTextByteCapacity = DefaultInputTextByteCapacity;
    u32 platformEventCapacity = DefaultPlatformEventCapacity;
};

enum class FrameBatchAppendResult : u8 {
    Appended,
    Coalesced,
    ResetInserted,
    IgnoredAfterReset,
    FrameNotOpen,
    SequenceExhausted,
    InvalidPayload,
};

// Production frame assembly performs all storage allocation at Create and never
// grows while polling. The extra array element is reserved exclusively for a
// reset marker; text input is copied into the fixed byte arena owned by this
// builder.
class PlatformFrameBuilder final {
  public:
    // M7-A treats the gamepad source registry slot as the fixed first-phase
    // routing index. Payload validation accepts a generation that is absent
    // from the final snapshot only when the same poll proves the corresponding
    // cancel/disconnect or raw-stream-reset lifecycle transition.
    static constexpr usize MaximumGamepadSlots = 16;
    static constexpr usize MaximumGamepads = MaximumGamepadSlots;

    [[nodiscard]] static Core::Result<PlatformFrameBuilder> Create(PlatformFrameCapacityConfig capacities = {})
    {
        if (capacities.inputTransitionCapacity == 0 ||
            capacities.inputTransitionCapacity > PlatformFrameCapacityConfig::MaximumInputTransitionCapacity ||
            capacities.inputTextByteCapacity == 0 ||
            capacities.inputTextByteCapacity > PlatformFrameCapacityConfig::MaximumInputTextByteCapacity ||
            capacities.platformEventCapacity == 0 ||
            capacities.platformEventCapacity > PlatformFrameCapacityConfig::MaximumPlatformEventCapacity)
        {
            return Core::failure(PlatformErrorCode::InvalidFrameCapacity,
                                 "Platform frame capacity is outside the supported range");
        }

        auto inputStorage = std::unique_ptr<InputTransition[]>(
            new (std::nothrow) InputTransition[static_cast<usize>(capacities.inputTransitionCapacity) + 1]);
        auto eventStorage = std::unique_ptr<PlatformEvent[]>(
            new (std::nothrow) PlatformEvent[static_cast<usize>(capacities.platformEventCapacity) + 1]);
        auto inputTextStorage = std::unique_ptr<char[]>(new (std::nothrow) char[capacities.inputTextByteCapacity]);
        if (inputStorage == nullptr || eventStorage == nullptr || inputTextStorage == nullptr)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "Platform frame storage allocation failed");
        }

        return PlatformFrameBuilder(capacities, std::move(inputStorage), std::move(eventStorage),
                                    std::move(inputTextStorage));
    }

    PlatformFrameBuilder(const PlatformFrameBuilder&) = delete;
    PlatformFrameBuilder& operator=(const PlatformFrameBuilder&) = delete;
    PlatformFrameBuilder(PlatformFrameBuilder&&) noexcept = default;
    PlatformFrameBuilder& operator=(PlatformFrameBuilder&&) noexcept = default;

    [[nodiscard]] Core::Status beginFrame(PlatformFrameId id)
    {
        if (frameOpen_)
        {
            return Core::failure(PlatformErrorCode::FrameBuilderState,
                                 "The previous platform frame has not been finalized");
        }
        if (!id.hasValue())
        {
            return Core::failure(PlatformErrorCode::FrameBuilderState, "A platform frame id must be non-zero");
        }
        if (sequenceExhausted_)
        {
            return Core::failure(PlatformErrorCode::PlatformSequenceExhausted,
                                 "The platform event sequence is exhausted");
        }

        frameId_ = id;
        windowCount_ = 0;
        gamepadCount_ = 0;
        inputCount_ = 0;
        eventCount_ = 0;
        inputTextByteCount_ = 0;
        inputResetWritten_ = false;
        eventResetWritten_ = false;
        invalidInputPayload_ = false;
        invalidPlatformEventPayload_ = false;
        previousAppendWasPointerMove_ = false;
        diagnostics_ = {};
        frameOpen_ = true;
        return Core::success();
    }

    [[nodiscard]] bool setPrimaryWindowSnapshot(WindowMetricsSnapshot metrics, WindowInputSnapshot input) noexcept
    {
        if (!frameOpen_ || metrics.window != input.window || metrics.revision != input.sourceMetricsRevision)
        {
            return false;
        }
        windows_[0] = WindowFrameSnapshot{std::move(metrics), std::move(input)};
        windowCount_ = 1;
        return true;
    }

    [[nodiscard]] bool setGamepadSnapshots(std::span<const GamepadSnapshot> snapshots) noexcept
    {
        if (!frameOpen_ || snapshots.size() > MaximumGamepadSlots)
        {
            return false;
        }
        std::ranges::copy(snapshots, gamepads_.begin());
        gamepadCount_ = snapshots.size();
        return true;
    }

    [[nodiscard]] FrameBatchAppendResult appendInputTransition(InputTransitionPayload payload) noexcept
    {
        if (!frameOpen_)
        {
            return FrameBatchAppendResult::FrameNotOpen;
        }

        const std::optional<u64> sequence = takeNextSequence();
        if (!sequence.has_value())
        {
            return FrameBatchAppendResult::SequenceExhausted;
        }
        if (inputResetWritten_)
        {
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::IgnoredAfterReset;
        }
        if (invalidInputPayload_)
        {
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::InvalidPayload;
        }
        if (!isValidInputTransitionPayload(payload))
        {
            invalidInputPayload_ = true;
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::InvalidPayload;
        }

        if (std::holds_alternative<InputStreamReset>(payload))
        {
            writeInputReset(*sequence, std::get<InputStreamReset>(std::move(payload)));
            return FrameBatchAppendResult::ResetInserted;
        }
        if (const FrameBatchAppendResult textResult = copyBorrowedTextIntoArena(payload, *sequence);
            textResult != FrameBatchAppendResult::Appended)
        {
            return textResult;
        }

        if (auto* move = std::get_if<PointerMoveTransition>(&payload);
            move != nullptr && previousAppendWasPointerMove_ && inputCount_ != 0)
        {
            auto& previous = inputStorage_[inputCount_ - 1];
            auto* previousMove = std::get_if<PointerMoveTransition>(&previous.payload);
            if (previousMove != nullptr && previousMove->window == move->window &&
                previousMove->pointer == move->pointer)
            {
                const double mergedDeltaX = previousMove->deltaX + move->deltaX;
                const double mergedDeltaY = previousMove->deltaY + move->deltaY;
                if (!isFinite(mergedDeltaX) || !isFinite(mergedDeltaY))
                {
                    invalidInputPayload_ = true;
                    previousAppendWasPointerMove_ = false;
                    return FrameBatchAppendResult::InvalidPayload;
                }
                previousMove->logicalX = move->logicalX;
                previousMove->logicalY = move->logicalY;
                previousMove->deltaX = mergedDeltaX;
                previousMove->deltaY = mergedDeltaY;
                previous.sequence = *sequence;
                return FrameBatchAppendResult::Coalesced;
            }
        }

        if (inputCount_ < capacities_.inputTransitionCapacity)
        {
            inputStorage_[inputCount_++] = InputTransition{*sequence, std::move(payload)};
            previousAppendWasPointerMove_ =
                std::holds_alternative<PointerMoveTransition>(inputStorage_[inputCount_ - 1].payload);
            return FrameBatchAppendResult::Appended;
        }

        ++diagnostics_.inputOverflowCount;
        writeInputReset(*sequence, InputStreamReset{.reason = InputResetReason::CapacityExceeded});
        return FrameBatchAppendResult::ResetInserted;
    }

    [[nodiscard]] FrameBatchAppendResult appendPlatformEvent(PlatformEventPayload payload) noexcept
    {
        if (!frameOpen_)
        {
            return FrameBatchAppendResult::FrameNotOpen;
        }

        previousAppendWasPointerMove_ = false;
        const std::optional<u64> sequence = takeNextSequence();
        if (!sequence.has_value())
        {
            return FrameBatchAppendResult::SequenceExhausted;
        }
        if (eventResetWritten_)
        {
            return FrameBatchAppendResult::IgnoredAfterReset;
        }
        if (invalidPlatformEventPayload_)
        {
            return FrameBatchAppendResult::InvalidPayload;
        }
        if (!isValidPlatformEventPayload(payload))
        {
            invalidPlatformEventPayload_ = true;
            return FrameBatchAppendResult::InvalidPayload;
        }

        if (std::holds_alternative<PlatformEventStreamReset>(payload))
        {
            writePlatformEventReset(*sequence, std::get<PlatformEventStreamReset>(std::move(payload)));
            return FrameBatchAppendResult::ResetInserted;
        }

        if (const auto* metrics = std::get_if<WindowMetricsChangedEvent>(&payload); metrics != nullptr)
        {
            for (usize index = 0; index < eventCount_; ++index)
            {
                const auto* existing = std::get_if<WindowMetricsChangedEvent>(&eventStorage_[index].payload);
                if (existing == nullptr || existing->window != metrics->window)
                {
                    continue;
                }
                for (usize shift = index + 1; shift < eventCount_; ++shift)
                {
                    eventStorage_[shift - 1] = std::move(eventStorage_[shift]);
                }
                --eventCount_;
                eventStorage_[eventCount_++] = PlatformEvent{*sequence, std::move(payload)};
                return FrameBatchAppendResult::Coalesced;
            }
        }

        if (eventCount_ < capacities_.platformEventCapacity)
        {
            eventStorage_[eventCount_++] = PlatformEvent{*sequence, std::move(payload)};
            return FrameBatchAppendResult::Appended;
        }

        ++diagnostics_.platformEventOverflowCount;
        writePlatformEventReset(*sequence, PlatformEventStreamReset{
                                               .reason = PlatformEventResetReason::CapacityExceeded,
                                           });
        return FrameBatchAppendResult::ResetInserted;
    }

    [[nodiscard]] Core::Result<PlatformFrameView> finishFrame()
    {
        if (!frameOpen_)
        {
            return Core::failure(PlatformErrorCode::FrameBuilderState, "No platform frame is open");
        }
        if (sequenceExhausted_)
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::PlatformSequenceExhausted,
                                 "The platform event sequence is exhausted");
        }
        if (invalidInputPayload_)
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidInputPayload,
                                 "The platform frame contains an invalid input payload");
        }
        if (invalidPlatformEventPayload_)
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidPlatformEventPayload,
                                 "The platform frame contains an invalid platform event payload");
        }
        if (!validateFinalSnapshots())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidFrameSnapshot,
                                 "The platform frame contains an invalid final snapshot");
        }
        if (!validateInputWindowRelationships())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidInputPayload, "The platform frame contains an input payload "
                                                                         "outside the primary window");
        }
        if (!validateGamepadCancellationRelationships())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidInputPayload,
                                 "The platform frame contains a gamepad cancellation "
                                 "outside the lifecycle state");
        }
        if (!validateGamepadInputRelationships())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidInputPayload, "The platform frame contains a gamepad input "
                                                                         "payload outside the lifecycle state");
        }
        if (!validateDigitalTransitionSnapshotRelationships())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidInputPayload,
                                 "The platform frame contains a digital transition "
                                 "inconsistent with final snapshots");
        }
        if (!validatePlatformEventSnapshotRelationships())
        {
            frameOpen_ = false;
            return Core::failure(PlatformErrorCode::InvalidPlatformEventPayload,
                                 "The platform frame contains a platform event "
                                 "inconsistent with final snapshots");
        }
        frameOpen_ = false;
        return PlatformFrameView(frameId_, std::span<const WindowFrameSnapshot>(windows_.data(), windowCount_),
                                 std::span<const GamepadSnapshot>(gamepads_.data(), gamepadCount_),
                                 std::span<const InputTransition>(inputStorage_.get(), inputCount_),
                                 std::span<const PlatformEvent>(eventStorage_.get(), eventCount_), diagnostics_);
    }

    // Abandons a partially assembled poll after a backend error or close
    // request. Global sequence numbers remain consumed so a later successful
    // frame can never reuse an identity already observed by a callback.
    [[nodiscard]] bool discardFrame() noexcept
    {
        if (!frameOpen_)
        {
            return false;
        }
        frameOpen_ = false;
        frameId_ = {};
        windowCount_ = 0;
        gamepadCount_ = 0;
        inputCount_ = 0;
        eventCount_ = 0;
        inputTextByteCount_ = 0;
        inputResetWritten_ = false;
        eventResetWritten_ = false;
        invalidInputPayload_ = false;
        invalidPlatformEventPayload_ = false;
        previousAppendWasPointerMove_ = false;
        diagnostics_ = {};
        return true;
    }

    [[nodiscard]] PlatformFrameCapacityConfig capacities() const noexcept
    {
        return capacities_;
    }

  private:
    PlatformFrameBuilder(PlatformFrameCapacityConfig capacities, std::unique_ptr<InputTransition[]> inputStorage,
                         std::unique_ptr<PlatformEvent[]> eventStorage,
                         std::unique_ptr<char[]> inputTextStorage) noexcept
        : capacities_(capacities), inputStorage_(std::move(inputStorage)), eventStorage_(std::move(eventStorage)),
          inputTextStorage_(std::move(inputTextStorage))
    {
    }

    [[nodiscard]] FrameBatchAppendResult copyTextViewIntoArena(std::string_view source, std::string_view& destination,
                                                               u64 resetSequence) noexcept
    {
        if (!Core::isStrictUtf8WithoutNul(source))
        {
            invalidInputPayload_ = true;
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::InvalidPayload;
        }
        if (source.size() > static_cast<usize>(capacities_.inputTextByteCapacity) - inputTextByteCount_)
        {
            ++diagnostics_.inputTextOverflowCount;
            writeInputReset(resetSequence, InputStreamReset{
                                               .reason = InputResetReason::TextByteCapacityExceeded,
                                           });
            return FrameBatchAppendResult::ResetInserted;
        }
        if (source.empty())
        {
            destination = {};
            return FrameBatchAppendResult::Appended;
        }

        char* target = inputTextStorage_.get() + inputTextByteCount_;
        std::memcpy(target, source.data(), source.size());
        inputTextByteCount_ += source.size();
        destination = std::string_view(target, source.size());
        return FrameBatchAppendResult::Appended;
    }

    [[nodiscard]] FrameBatchAppendResult copyBorrowedTextIntoArena(InputTransitionPayload& payload,
                                                                   u64 resetSequence) noexcept
    {
        if (auto* text = std::get_if<TextInputTransition>(&payload); text != nullptr)
        {
            const FrameBatchAppendResult result =
                copyTextViewIntoArena(text->committedUtf8, text->committedUtf8, resetSequence);
            if (result != FrameBatchAppendResult::Appended)
            {
                return result;
            }
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::Appended;
        }
        if (auto* composition = std::get_if<TextCompositionTransition>(&payload); composition != nullptr)
        {
            const std::optional<u32> preeditCodepoints =
                Core::countStrictUtf8CodepointsWithoutNul(composition->preeditUtf8);
            if (!preeditCodepoints.has_value() || composition->cursorCodepoint > *preeditCodepoints)
            {
                invalidInputPayload_ = true;
                previousAppendWasPointerMove_ = false;
                return FrameBatchAppendResult::InvalidPayload;
            }
            const FrameBatchAppendResult result =
                copyTextViewIntoArena(composition->preeditUtf8, composition->preeditUtf8, resetSequence);
            if (result != FrameBatchAppendResult::Appended)
            {
                return result;
            }
            previousAppendWasPointerMove_ = false;
            return FrameBatchAppendResult::Appended;
        }
        return FrameBatchAppendResult::Appended;
    }

    [[nodiscard]] static bool isFinite(double value) noexcept
    {
        return std::isfinite(value);
    }

    [[nodiscard]] static bool isFinite(float value) noexcept
    {
        return std::isfinite(value);
    }

    [[nodiscard]] static bool isValidDigitalTransition(DigitalTransition state) noexcept
    {
        return state == DigitalTransition::Down || state == DigitalTransition::Up;
    }

    [[nodiscard]] static bool isValidKey(Key key) noexcept
    {
        return key > Key::Unknown && key < Key::Count;
    }

    [[nodiscard]] static bool isValidPointerButton(PointerButton button) noexcept
    {
        return button < PointerButton::Count;
    }

    [[nodiscard]] static bool isValidGamepadButton(GamepadButton button) noexcept
    {
        return button < GamepadButton::Count;
    }

    [[nodiscard]] static bool isValidGamepadAxis(GamepadAxis axis) noexcept
    {
        return axis < GamepadAxis::Count;
    }

    [[nodiscard]] static bool isValidGamepadId(GamepadId gamepad) noexcept
    {
        return gamepad.hasValue() && gamepad.index() < MaximumGamepadSlots;
    }

    [[nodiscard]] static bool isValidInputCancelReason(InputCancelReason reason) noexcept
    {
        return reason == InputCancelReason::FocusLost || reason == InputCancelReason::DeviceDisconnected ||
               reason == InputCancelReason::WindowClosing || reason == InputCancelReason::BackendRecovery;
    }

    [[nodiscard]] static bool isValidInputCancelTransition(const InputCancelTransition& transition) noexcept
    {
        if (!transition.routedWindow.hasValue() || !isValidInputCancelReason(transition.reason))
        {
            return false;
        }
        if (transition.reason == InputCancelReason::DeviceDisconnected)
        {
            return transition.gamepad.has_value() && isValidGamepadId(*transition.gamepad);
        }
        return !transition.gamepad.has_value();
    }

    [[nodiscard]] static bool isValidInputResetReason(InputResetReason reason) noexcept
    {
        return reason == InputResetReason::CapacityExceeded || reason == InputResetReason::TextByteCapacityExceeded ||
               reason == InputResetReason::BackendRecovery;
    }

    [[nodiscard]] static bool isValidTextCompositionStage(TextCompositionStage stage) noexcept
    {
        return stage == TextCompositionStage::Started || stage == TextCompositionStage::Updated ||
               stage == TextCompositionStage::Ended || stage == TextCompositionStage::Cancelled;
    }

    [[nodiscard]] static bool isValidPlatformEventResetReason(PlatformEventResetReason reason) noexcept
    {
        return reason == PlatformEventResetReason::CapacityExceeded ||
               reason == PlatformEventResetReason::BackendRecovery;
    }

    [[nodiscard]] static bool isValidInputTransitionPayload(const InputTransitionPayload& payload) noexcept
    {
        if (const auto* value = std::get_if<KeyTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue() && isValidKey(value->key) && isValidDigitalTransition(value->state) &&
                   (!value->repeat || value->state == DigitalTransition::Down);
        }
        if (const auto* value = std::get_if<PointerButtonTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue() && value->pointer == PrimaryPointerId &&
                   isValidPointerButton(value->button) && isValidDigitalTransition(value->state) &&
                   isFinite(value->logicalX) && isFinite(value->logicalY);
        }
        if (const auto* value = std::get_if<PointerMoveTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue() && value->pointer == PrimaryPointerId && isFinite(value->logicalX) &&
                   isFinite(value->logicalY) && isFinite(value->deltaX) && isFinite(value->deltaY);
        }
        if (const auto* value = std::get_if<PointerWheelTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue() && value->pointer == PrimaryPointerId && isFinite(value->logicalX) &&
                   isFinite(value->logicalY) && isFinite(value->deltaX) && isFinite(value->deltaY);
        }
        if (const auto* value = std::get_if<GamepadButtonTransition>(&payload); value != nullptr)
        {
            return value->routedWindow.hasValue() && isValidGamepadId(value->gamepad) &&
                   isValidGamepadButton(value->button) && isValidDigitalTransition(value->state);
        }
        if (const auto* value = std::get_if<GamepadAxisTransition>(&payload); value != nullptr)
        {
            return value->routedWindow.hasValue() && isValidGamepadId(value->gamepad) &&
                   isValidGamepadAxis(value->axis) && isFinite(value->value);
        }
        if (const auto* value = std::get_if<TextInputTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue();
        }
        if (const auto* value = std::get_if<TextCompositionTransition>(&payload); value != nullptr)
        {
            return value->window.hasValue() && isValidTextCompositionStage(value->stage);
        }
        if (const auto* value = std::get_if<InputCancelTransition>(&payload); value != nullptr)
        {
            return isValidInputCancelTransition(*value);
        }
        if (const auto* value = std::get_if<InputStreamReset>(&payload); value != nullptr)
        {
            return isValidInputResetReason(value->reason) &&
                   (!value->routedWindow.has_value() || value->routedWindow->hasValue());
        }
        return false;
    }

    [[nodiscard]] static bool isValidPlatformEventPayload(const PlatformEventPayload& payload) noexcept
    {
        if (const auto* value = std::get_if<WindowMetricsChangedEvent>(&payload); value != nullptr)
        {
            return value->window.hasValue() && value->metricsRevision != 0;
        }
        if (const auto* value = std::get_if<GamepadConnectedEvent>(&payload); value != nullptr)
        {
            return isValidGamepadId(value->gamepad);
        }
        if (const auto* value = std::get_if<GamepadDisconnectedEvent>(&payload); value != nullptr)
        {
            return isValidGamepadId(value->gamepad);
        }
        if (const auto* value = std::get_if<PlatformEventStreamReset>(&payload); value != nullptr)
        {
            return isValidPlatformEventResetReason(value->reason);
        }
        return false;
    }

    [[nodiscard]] static bool isValidWindowSnapshot(const WindowFrameSnapshot& snapshot) noexcept
    {
        const WindowMetricsSnapshot& metrics = snapshot.metrics;
        const WindowInputSnapshot& input = snapshot.input;
        return metrics.window.hasValue() && metrics.window == input.window && metrics.revision != 0 &&
               metrics.revision == input.sourceMetricsRevision && metrics.logicalExtent.width != 0 &&
               metrics.logicalExtent.height != 0 && isFinite(metrics.contentScale.x) &&
               isFinite(metrics.contentScale.y) && metrics.contentScale.x > 0.0F && metrics.contentScale.y > 0.0F &&
               input.pointer.pointer == PrimaryPointerId && isFinite(input.pointer.logicalX) &&
               isFinite(input.pointer.logicalY) && isFinite(input.pointer.accumulatedDeltaX) &&
               isFinite(input.pointer.accumulatedDeltaY);
    }

    [[nodiscard]] static bool isValidGamepadSnapshot(const GamepadSnapshot& snapshot) noexcept
    {
        if (!isValidGamepadId(snapshot.gamepad) || snapshot.revision == 0)
        {
            return false;
        }
        return std::ranges::all_of(snapshot.axes, [](float value) noexcept { return isFinite(value); });
    }

    [[nodiscard]] bool validateFinalSnapshots() const noexcept
    {
        for (usize index = 0; index < windowCount_; ++index)
        {
            if (!isValidWindowSnapshot(windows_[index]))
            {
                return false;
            }
        }
        for (usize index = 0; index < gamepadCount_; ++index)
        {
            if (!isValidGamepadSnapshot(gamepads_[index]))
            {
                return false;
            }
            for (usize previous = 0; previous < index; ++previous)
            {
                const GamepadId previousId = gamepads_[previous].gamepad;
                const GamepadId currentId = gamepads_[index].gamepad;
                if (previousId.owner() != currentId.owner() || previousId.index() == currentId.index())
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] const WindowId* primaryWindowId() const noexcept
    {
        if (windowCount_ != 1)
        {
            return nullptr;
        }
        return &windows_[0].metrics.window;
    }

    [[nodiscard]] bool matchesPrimaryWindow(WindowId window) const noexcept
    {
        const WindowId* primary = primaryWindowId();
        return primary != nullptr && window == *primary;
    }

    [[nodiscard]] bool hasFinalGamepadSnapshot(GamepadId gamepad) const noexcept
    {
        for (usize index = 0; index < gamepadCount_; ++index)
        {
            if (gamepads_[index].gamepad == gamepad)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const GamepadSnapshot* finalGamepadSnapshot(GamepadId gamepad) const noexcept
    {
        for (usize index = 0; index < gamepadCount_; ++index)
        {
            if (gamepads_[index].gamepad == gamepad)
            {
                return &gamepads_[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool hasInputResetCoveringPrimaryWindow(u64 eventSequence) const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            if (input.sequence >= eventSequence)
            {
                continue;
            }
            const auto* reset = std::get_if<InputStreamReset>(&input.payload);
            if (reset == nullptr)
            {
                continue;
            }
            if (!reset->routedWindow.has_value() || matchesPrimaryWindow(*reset->routedWindow))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasEarlierDeviceDisconnectCancellationProof(GamepadId gamepad, u64 eventSequence) const noexcept
    {
        if (hasInputResetCoveringPrimaryWindow(eventSequence))
        {
            return true;
        }
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            if (input.sequence >= eventSequence)
            {
                continue;
            }
            const auto* cancel = std::get_if<InputCancelTransition>(&input.payload);
            if (cancel == nullptr || cancel->reason != InputCancelReason::DeviceDisconnected ||
                !cancel->gamepad.has_value() || *cancel->gamepad != gamepad)
            {
                continue;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool hasLaterMatchingDisconnect(GamepadId gamepad, u64 eventSequence) const noexcept
    {
        for (usize index = 0; index < eventCount_; ++index)
        {
            const PlatformEvent& event = eventStorage_[index];
            if (event.sequence <= eventSequence)
            {
                continue;
            }
            const auto* disconnected = std::get_if<GamepadDisconnectedEvent>(&event.payload);
            if (disconnected != nullptr && disconnected->gamepad == gamepad)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasEarlierMatchingDisconnect(GamepadId gamepad, u64 inputSequence) const noexcept
    {
        for (usize index = 0; index < eventCount_; ++index)
        {
            const PlatformEvent& event = eventStorage_[index];
            if (event.sequence >= inputSequence)
            {
                continue;
            }
            const auto* disconnected = std::get_if<GamepadDisconnectedEvent>(&event.payload);
            if (disconnected != nullptr && disconnected->gamepad == gamepad)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasLaterInputResetCoveringPrimaryWindow(u64 inputSequence) const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            if (input.sequence <= inputSequence)
            {
                continue;
            }
            const auto* reset = std::get_if<InputStreamReset>(&input.payload);
            if (reset == nullptr)
            {
                continue;
            }
            if (!reset->routedWindow.has_value() || matchesPrimaryWindow(*reset->routedWindow))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasDeviceDisconnectCancelBetween(GamepadId gamepad, u64 afterSequence,
                                                        u64 beforeSequence) const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            if (input.sequence <= afterSequence || input.sequence >= beforeSequence)
            {
                continue;
            }
            const auto* cancel = std::get_if<InputCancelTransition>(&input.payload);
            if (cancel != nullptr && cancel->reason == InputCancelReason::DeviceDisconnected &&
                cancel->gamepad.has_value() && *cancel->gamepad == gamepad)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasEarlierDeviceDisconnectCancel(GamepadId gamepad, u64 inputSequence) const noexcept
    {
        return hasDeviceDisconnectCancelBetween(gamepad, 0, inputSequence);
    }

    [[nodiscard]] bool hasLaterDisconnectWithCancellationProof(GamepadId gamepad, u64 inputSequence) const noexcept
    {
        for (usize index = 0; index < eventCount_; ++index)
        {
            const PlatformEvent& event = eventStorage_[index];
            if (event.sequence <= inputSequence)
            {
                continue;
            }
            const auto* disconnected = std::get_if<GamepadDisconnectedEvent>(&event.payload);
            if (disconnected == nullptr || disconnected->gamepad != gamepad)
            {
                continue;
            }
            if (hasDeviceDisconnectCancelBetween(gamepad, inputSequence, event.sequence))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool validateInputWindowRelationships() const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransitionPayload& payload = inputStorage_[index].payload;
            std::optional<WindowId> routedWindow{};
            if (const auto* value = std::get_if<KeyTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<PointerButtonTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<PointerMoveTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<PointerWheelTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<GamepadButtonTransition>(&payload); value != nullptr)
            {
                routedWindow = value->routedWindow;
            } else if (const auto* value = std::get_if<GamepadAxisTransition>(&payload); value != nullptr)
            {
                routedWindow = value->routedWindow;
            } else if (const auto* value = std::get_if<TextInputTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<TextCompositionTransition>(&payload); value != nullptr)
            {
                routedWindow = value->window;
            } else if (const auto* value = std::get_if<InputCancelTransition>(&payload); value != nullptr)
            {
                routedWindow = value->routedWindow;
            } else if (const auto* value = std::get_if<InputStreamReset>(&payload);
                       value != nullptr && value->routedWindow.has_value())
            {
                routedWindow = *value->routedWindow;
            }

            if (routedWindow.has_value() && !matchesPrimaryWindow(*routedWindow))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validateGamepadInputRelationships() const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            std::optional<GamepadId> gamepad{};
            if (const auto* value = std::get_if<GamepadButtonTransition>(&input.payload); value != nullptr)
            {
                gamepad = value->gamepad;
            } else if (const auto* value = std::get_if<GamepadAxisTransition>(&input.payload); value != nullptr)
            {
                gamepad = value->gamepad;
            }
            if (!gamepad.has_value())
            {
                continue;
            }
            if (hasEarlierMatchingDisconnect(*gamepad, input.sequence) ||
                hasEarlierDeviceDisconnectCancel(*gamepad, input.sequence))
            {
                return false;
            }
            if (hasFinalGamepadSnapshot(*gamepad))
            {
                continue;
            }
            if (hasLaterDisconnectWithCancellationProof(*gamepad, input.sequence) ||
                hasLaterInputResetCoveringPrimaryWindow(input.sequence))
            {
                continue;
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateGamepadCancellationRelationships() const noexcept
    {
        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransition& input = inputStorage_[index];
            const auto* cancel = std::get_if<InputCancelTransition>(&input.payload);
            if (cancel == nullptr || cancel->reason != InputCancelReason::DeviceDisconnected ||
                !cancel->gamepad.has_value())
            {
                continue;
            }
            if (hasFinalGamepadSnapshot(*cancel->gamepad))
            {
                return false;
            }
            if (!eventResetWritten_ && !hasLaterMatchingDisconnect(*cancel->gamepad, input.sequence))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validateDigitalTransitionSnapshotRelationships() const noexcept
    {
        if (windowCount_ != 1)
        {
            // Window-routed payloads are rejected by
            // validateInputWindowRelationships(); a global reset remains valid
            // for headless or already-closed-window recovery frames.
            return true;
        }

        enum class ExpectedDigitalState : u8 {
            Unspecified,
            Released,
            Held,
        };
        const auto expectedState = [](DigitalTransition transition) noexcept {
            return transition == DigitalTransition::Down ? ExpectedDigitalState::Held : ExpectedDigitalState::Released;
        };
        std::array<ExpectedDigitalState, KeyCount> keyExpectations{};
        std::array<ExpectedDigitalState, PointerButtonCount> pointerExpectations{};
        std::array<std::array<ExpectedDigitalState, GamepadButtonCount>, MaximumGamepadSlots> gamepadExpectations{};
        std::array<GamepadId, MaximumGamepadSlots> expectedGamepads{};

        const auto clearAllExpectations = [&]() noexcept {
            keyExpectations.fill(ExpectedDigitalState::Unspecified);
            pointerExpectations.fill(ExpectedDigitalState::Unspecified);
            for (auto& expectations : gamepadExpectations)
            {
                expectations.fill(ExpectedDigitalState::Unspecified);
            }
            expectedGamepads.fill(GamepadId{});
        };

        for (usize index = 0; index < inputCount_; ++index)
        {
            const InputTransitionPayload& payload = inputStorage_[index].payload;
            if (const auto* key = std::get_if<KeyTransition>(&payload); key != nullptr)
            {
                keyExpectations[static_cast<usize>(key->key)] = expectedState(key->state);
                continue;
            }
            if (const auto* pointer = std::get_if<PointerButtonTransition>(&payload); pointer != nullptr)
            {
                pointerExpectations[static_cast<usize>(pointer->button)] = expectedState(pointer->state);
                continue;
            }
            if (const auto* gamepad = std::get_if<GamepadButtonTransition>(&payload); gamepad != nullptr)
            {
                const usize slot = gamepad->gamepad.index();
                if (expectedGamepads[slot] != gamepad->gamepad)
                {
                    gamepadExpectations[slot].fill(ExpectedDigitalState::Unspecified);
                    expectedGamepads[slot] = gamepad->gamepad;
                }
                gamepadExpectations[slot][static_cast<usize>(gamepad->button)] = expectedState(gamepad->state);
                continue;
            }
            if (const auto* cancel = std::get_if<InputCancelTransition>(&payload); cancel != nullptr)
            {
                if (!cancel->gamepad.has_value())
                {
                    clearAllExpectations();
                    continue;
                }
                const usize slot = cancel->gamepad->index();
                if (expectedGamepads[slot] == *cancel->gamepad)
                {
                    gamepadExpectations[slot].fill(ExpectedDigitalState::Unspecified);
                    expectedGamepads[slot] = GamepadId{};
                }
                continue;
            }
            if (std::holds_alternative<InputStreamReset>(payload))
            {
                clearAllExpectations();
            }
        }

        const WindowInputSnapshot& finalWindow = windows_[0].input;
        for (usize keyIndex = 0; keyIndex < keyExpectations.size(); ++keyIndex)
        {
            const ExpectedDigitalState expected = keyExpectations[keyIndex];
            if (expected != ExpectedDigitalState::Unspecified &&
                finalWindow.heldKeys.test(keyIndex) != (expected == ExpectedDigitalState::Held))
            {
                return false;
            }
        }
        for (usize buttonIndex = 0; buttonIndex < pointerExpectations.size(); ++buttonIndex)
        {
            const ExpectedDigitalState expected = pointerExpectations[buttonIndex];
            if (expected != ExpectedDigitalState::Unspecified &&
                finalWindow.pointer.heldButtons.test(buttonIndex) != (expected == ExpectedDigitalState::Held))
            {
                return false;
            }
        }
        for (usize slot = 0; slot < expectedGamepads.size(); ++slot)
        {
            if (!expectedGamepads[slot].hasValue())
            {
                continue;
            }
            const GamepadSnapshot* finalSnapshot = finalGamepadSnapshot(expectedGamepads[slot]);
            if (finalSnapshot == nullptr)
            {
                continue;
            }
            for (usize buttonIndex = 0; buttonIndex < gamepadExpectations[slot].size(); ++buttonIndex)
            {
                const ExpectedDigitalState expected = gamepadExpectations[slot][buttonIndex];
                if (expected != ExpectedDigitalState::Unspecified &&
                    finalSnapshot->heldButtons.test(buttonIndex) != (expected == ExpectedDigitalState::Held))
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool validatePlatformEventSnapshotRelationships() const noexcept
    {
        for (usize index = 0; index < eventCount_; ++index)
        {
            const PlatformEvent& event = eventStorage_[index];
            if (const auto* metrics = std::get_if<WindowMetricsChangedEvent>(&event.payload); metrics != nullptr)
            {
                if (eventResetWritten_)
                {
                    continue;
                }
                if (windowCount_ != 1 || metrics->window != windows_[0].metrics.window ||
                    metrics->metricsRevision != windows_[0].metrics.revision)
                {
                    return false;
                }
                continue;
            }
            if (const auto* connected = std::get_if<GamepadConnectedEvent>(&event.payload); connected != nullptr)
            {
                if (eventResetWritten_)
                {
                    continue;
                }
                if (!hasFinalGamepadSnapshot(connected->gamepad) &&
                    !hasLaterMatchingDisconnect(connected->gamepad, event.sequence))
                {
                    return false;
                }
                continue;
            }
            if (const auto* disconnected = std::get_if<GamepadDisconnectedEvent>(&event.payload);
                disconnected != nullptr)
            {
                // A lifecycle-stream reset lets observers rebuild their
                // registry, but it does not cancel gameplay/UI input. Every
                // retained disconnect still needs an earlier raw cancel/reset.
                if (hasFinalGamepadSnapshot(disconnected->gamepad) ||
                    !hasEarlierDeviceDisconnectCancellationProof(disconnected->gamepad, event.sequence))
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<u64> takeNextSequence() noexcept
    {
        if (nextSequence_ == (std::numeric_limits<u64>::max)())
        {
            sequenceExhausted_ = true;
            return std::nullopt;
        }
        return nextSequence_++;
    }

    void writeInputReset(u64 sequence, InputStreamReset reset) noexcept
    {
        inputStorage_[inputCount_++] = InputTransition{sequence, std::move(reset)};
        inputResetWritten_ = true;
        previousAppendWasPointerMove_ = false;
    }

    void writePlatformEventReset(u64 sequence, PlatformEventStreamReset reset) noexcept
    {
        eventStorage_[eventCount_++] = PlatformEvent{sequence, std::move(reset)};
        eventResetWritten_ = true;
    }

    PlatformFrameCapacityConfig capacities_{};
    std::unique_ptr<InputTransition[]> inputStorage_;
    std::unique_ptr<PlatformEvent[]> eventStorage_;
    std::unique_ptr<char[]> inputTextStorage_;
    std::array<WindowFrameSnapshot, 1> windows_{};
    std::array<GamepadSnapshot, MaximumGamepadSlots> gamepads_{};
    usize windowCount_ = 0;
    usize gamepadCount_ = 0;
    usize inputCount_ = 0;
    usize eventCount_ = 0;
    usize inputTextByteCount_ = 0;
    u64 nextSequence_ = 1;
    PlatformFrameId frameId_{};
    PlatformFrameDiagnostics diagnostics_{};
    bool frameOpen_ = false;
    bool inputResetWritten_ = false;
    bool eventResetWritten_ = false;
    bool invalidInputPayload_ = false;
    bool invalidPlatformEventPayload_ = false;
    bool previousAppendWasPointerMove_ = false;
    bool sequenceExhausted_ = false;
};

static_assert(std::is_nothrow_move_assignable_v<InputTransition>);
static_assert(std::is_nothrow_move_assignable_v<PlatformEvent>);
static_assert(std::is_nothrow_default_constructible_v<InputTransition>);
static_assert(std::is_nothrow_default_constructible_v<PlatformEvent>);

} // namespace Tina::Platform
