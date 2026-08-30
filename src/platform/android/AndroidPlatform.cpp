#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/android/AndroidPlatformFactory.hpp>

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#include "AndroidCompositionSession.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Platform {
namespace {

// Both ids are generation-checked handles, so they can only come from a pool, and the pool must
// outlive every id it hands out -- hence the backend owns both. Neither carries per-entry state:
// an Android activity has exactly one native window, so capacity is 1 and the record is empty.
struct AndroidWindowRecord final {};
struct AndroidWindowSurfaceRecord final {};
using WindowPool = Core::GenerationPool<AndroidWindowRecord, WindowRegistryTag>;
using SurfacePool = Core::GenerationPool<AndroidWindowSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

// Shared by creation and by window replacement, so the two cannot drift apart -- a rebind that
// accepted geometry the factory rejects would be a hole straight into bgfx::reset.
[[nodiscard]] Core::Status validateWindowGeometry(AndroidNativeWindowHandle window,
                                                 FramebufferExtent framebufferExtent,
                                                 ContentScale contentScale) noexcept
{
    if (window.nativeWindow == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The Android platform backend requires a live ANativeWindow handle");
    }
    // Zero extent or non-positive scale is rejected rather than defaulted: both silently mis-size
    // every UI element downstream, which is far harder to diagnose than a startup failure.
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The Android platform backend requires a non-empty framebuffer extent");
    }
    if (!(contentScale.x > 0.0F) || !(contentScale.y > 0.0F))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The Android platform backend requires a positive content scale");
    }
    return Core::success();
}

[[nodiscard]] WindowMetricsSnapshot makeWindowMetrics(WindowId window, FramebufferExtent framebufferExtent,
                                                     ContentScale contentScale, u64 revision) noexcept
{
    return WindowMetricsSnapshot{
        .window = window,
        // Logical size is derived, not queried: Android reports physical pixels, and the display
        // density is the only conversion available.
        .logicalExtent =
            LogicalExtent{
                static_cast<u32>(static_cast<float>(framebufferExtent.width) / contentScale.x),
                static_cast<u32>(static_cast<float>(framebufferExtent.height) / contentScale.y),
            },
        .framebufferExtent = framebufferExtent,
        .contentScale = contentScale,
        .revision = revision,
        // An activity that owns a live window is foreground and visible. Losing either is a
        // lifecycle transition the host drives, not an initial state.
        .focused = true,
        .minimized = false,
        .visible = true,
    };
}

class AndroidWindowSurfacePlatformBackend final : public Integration::IWindowSurfacePlatformBackend,
                                                  public IAndroidPlatformBackend {
  public:
    AndroidWindowSurfacePlatformBackend(
        PlatformFrameBuilder frameBuilder,
        WindowPool windowPool,
        SurfacePool surfacePool,
        std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl,
        std::shared_ptr<AndroidTouchEventQueue> touchEvents,
        std::shared_ptr<AndroidKeyEventQueue> keyEvents,
        std::shared_ptr<AndroidTextEventQueue> textEvents,
        std::shared_ptr<AndroidCompositionEventQueue> compositionEvents,
        Integration::WindowSurfaceId surfaceId,
        std::uintptr_t nativeWindow,
        WindowMetricsSnapshot metrics) noexcept
        : frameBuilder_(std::move(frameBuilder)),
          windowPool_(std::move(windowPool)),
          surfacePool_(std::move(surfacePool)),
          leaseControl_(std::move(leaseControl)),
          touchEvents_(std::move(touchEvents)),
          keyEvents_(std::move(keyEvents)),
          textEvents_(std::move(textEvents)),
          compositionEvents_(std::move(compositionEvents)),
          surfaceId_(surfaceId),
          nativeWindow_(nativeWindow),
          metrics_(metrics),
          pointerState_(quiescentInput(metrics)),
          surfaceSnapshot_(makeSurfaceSnapshot(surfaceId, metrics, 1)),
          ownerThread_(std::this_thread::get_id())
    {
    }

    ~AndroidWindowSurfacePlatformBackend() noexcept override
    {
        if (leaseControl_ != nullptr)
        {
            leaseControl_->surfaceAlive = false;
        }
    }

    [[nodiscard]] Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() override
    {
        if (auto status = checkUsable("initial primary window metrics"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        return std::optional<WindowMetricsSnapshot>{metrics_};
    }

    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame() override
    {
        if (auto status = checkUsable("poll"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (nextFrameId_ == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::FrameSequenceExhausted,
                                 "The Android platform frame sequence is exhausted");
        }

        auto beginStatus = frameBuilder_.beginFrame(PlatformFrameId{nextFrameId_++});
        if (!beginStatus)
        {
            return std::unexpected(std::move(beginStatus.error()));
        }
        // Transitions first, then the end-of-poll snapshot. Order matters: a transition carries the
        // position captured at that exact moment, while the snapshot is the state the poll ended
        // in, and downstream relies on both being consistent with each other.
        if (auto status = drainTouchEvents(); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (auto status = drainKeyEvents(); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (auto status = drainTextEvents(); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (auto status = drainCompositionEvents(); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics_, pointerState_))
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "The Android platform backend could not publish its window snapshot");
        }
        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return std::unexpected(std::move(frame.error()));
        }
        // A completed poll is the observation point for the surface revision: the consumer reads the
        // snapshot once per frame, so any further lifecycle change now needs its own revision. Doing
        // this here rather than in an explicit host call means a host cannot forget it.
        surfaceRevisionPendingObservation_ = false;
        return PlatformPollResult::Continue(*frame);
    }

    Core::Status updateTextInputPlacement(std::optional<TextInputPlacement> placement) override
    {
        if (auto status = checkUsable("text input placement"); !status)
        {
            return status;
        }
        if (!placement.has_value())
        {
            caretPixels_.reset();
            return Core::success();
        }
        // Rejecting a non-null placement here used to be the honest answer -- there was no IME
        // integration to hand it to. It also made the engine unusable with a focused TextEdit: the
        // Runtime publishes a caret every frame one is focused, the coordinator turns this failure into
        // a LifecycleInvariantViolation, and EngineHost latches a terminal outcome. The app simply
        // stopped producing frames, with the caret as the only cause.
        //
        // So the placement is latched instead. The host reads it and hands it to
        // InputMethodManager.updateCursorAnchorInfo when the IME has asked for cursor updates; unlike
        // IMM32 there is nothing to position from C++, because Android's candidate window belongs to
        // the IME process.
        if (placement->window != metrics_.window)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The Android text input placement belongs to another window");
        }
        const TextInputCaretRect& caret = placement->caret;
        // Validated rather than clamped: a non-finite or negative-height caret means the UI published
        // geometry it should not have, and silently substituting a plausible rectangle would place the
        // candidate window somewhere arbitrary with nothing to trace it back to.
        if (!isFinitePositiveCaret(caret))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The Android text input caret geometry must be finite with a positive height");
        }
        auto pixels = toCaretPixels(caret);
        if (!pixels)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The Android text input caret exceeds the native pixel range");
        }
        caretPixels_ = *pixels;
        return Core::success();
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
        if (leaseControl_ != nullptr)
        {
            leaseControl_->surfaceAlive = false;
        }
    }

    [[nodiscard]] Core::Status publishPrimaryWindow() noexcept override
    {
        if (auto status = checkUsable("publish primary window"); !status)
        {
            return status;
        }
        // Android has already shown the surface by the time the host owns an ANativeWindow;
        // there is no hidden-window phase to end, so publication is a state assertion rather
        // than an action.
        published_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Result<Integration::NativeWindowSurfaceLease>
    acquirePrimaryWindowSurfaceLease() noexcept override
    {
        if (auto status = checkUsable("window surface lease"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (leaseAcquired_)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired,
                                 "The Android primary WindowSurface lease was already acquired");
        }

        const Integration::Detail::NativeWindowBinding binding{
            .kind = Integration::Detail::NativeWindowBindingKind::Android,
            // ANativeWindow* is self-contained; the bgfx decoder rejects a display here.
            .nativeDisplay = 0,
            .nativeWindow = nativeWindow_,
            .bindingRevision = 1,
        };
        auto lease =
            Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(leaseControl_, surfaceId_, binding);
        if (!lease)
        {
            return std::unexpected(std::move(lease.error()));
        }
        leaseAcquired_ = true;
        return std::move(*lease);
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    primaryWindowSurfaceSnapshot() const noexcept override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The Android primary WindowSurface snapshot is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The Android WindowSurface snapshot must be read on the owner thread");
        }
        return surfaceSnapshot_;
    }

    // --- IAndroidPlatformBackend ---

    Core::Status onNativeWindowDestroyed() noexcept override
    {
        if (auto status = checkUsable("native window destroyed"); !status)
        {
            return status;
        }
        if (nativeWindow_ == 0)
        {
            // Idempotent: Android can deliver TERM_WINDOW without a preceding INIT_WINDOW during
            // teardown, and failing there would turn a normal lifecycle into an error.
            return Core::success();
        }

        nativeWindow_ = 0;
        // Suspended, not "resized to nothing": the GPU resources are genuinely gone, and ADR 0034
        // is explicit that surfaceSuspended does not mean device lost -- the device survives, only
        // the backbuffer does not.
        if (auto status = markSurfaceFactsChanged(); !status)
        {
            return status;
        }
        surfaceSnapshot_.suspended = true;

        // Every finger is gone with the window. Leaving slots mapped is precisely the cocos2d-x
        // failure where a drag interrupted by a task switch stranded its finger until process exit.
        releaseAllPointers();

        // A composition in flight goes with the window too. Losing the surface delivers no
        // InputConnection call at all, so without this the UI keeps drawing a preedit the IME has
        // already forgotten, and the next composing pass would report Updated for a session the
        // consumer never saw start. Queued into the composition ring rather than appended here: this
        // runs outside a frame, and appending to a builder that has not begun a frame is a failure.
        if (composition_.active())
        {
            compositionCancelPending_ = true;
        }

        // The caret described a window that no longer exists. Keeping it would have the host report a
        // stale anchor for the next surface, whose geometry may differ entirely.
        caretPixels_.reset();
        return Core::success();
    }

    Core::Status onNativeWindowCreated(AndroidNativeWindowHandle window, FramebufferExtent framebufferExtent,
                                       ContentScale contentScale) noexcept override
    {
        if (auto status = checkUsable("native window created"); !status)
        {
            return status;
        }
        if (auto status = validateWindowGeometry(window, framebufferExtent, contentScale); !status)
        {
            return status;
        }

        nativeWindow_ = window.nativeWindow;
        metrics_ = makeWindowMetrics(metrics_.window, framebufferExtent, contentScale, metrics_.revision + 1);
        // pointerState_ must be rebuilt, not carried: it holds the previous metrics revision, and a
        // published frame is rejected unless the two agree.
        pointerState_ = quiescentInput(metrics_);

        if (auto status = markSurfaceFactsChanged(); !status)
        {
            return status;
        }
        // The tracker requires all three to move together, so a backend can never observe the
        // rebind and the geometry it must reset to in two different frames.
        if (surfaceSnapshot_.nativeBindingRevision == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The Android native binding revision is exhausted");
        }
        ++surfaceSnapshot_.nativeBindingRevision;
        surfaceSnapshot_.framebufferExtent = framebufferExtent;
        surfaceSnapshot_.contentScale = contentScale;
        surfaceSnapshot_.sourceMetricsRevision = metrics_.revision;
        surfaceSnapshot_.suspended = false;

        // A replaced window means a new binding, so the previous lease no longer describes reality.
        // Allowing a fresh one is the whole point of a rebind.
        leaseAcquired_ = false;
        return Core::success();
    }

    [[nodiscard]] u64 droppedTouchEventCount() const noexcept override
    {
        return touchEvents_ == nullptr ? 0 : touchEvents_->droppedEventCount();
    }

    [[nodiscard]] u64 publishedTextCommitCount() const noexcept override
    {
        return publishedTextCommits_;
    }

    [[nodiscard]] Core::Status requestShowSoftKeyboard() noexcept override
    {
        return setSoftKeyboardRequest(AndroidSoftKeyboardRequest::Show);
    }

    [[nodiscard]] Core::Status requestHideSoftKeyboard() noexcept override
    {
        return setSoftKeyboardRequest(AndroidSoftKeyboardRequest::Hide);
    }

    [[nodiscard]] Core::Status onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept override
    {
        if (auto status = checkUsable("soft keyboard occlusion"); !status)
        {
            return status;
        }
        // A keyboard taller than the window means the host measured against different geometry
        // than the one this backend holds; clamping would hide that disagreement.
        if (occludedPhysicalHeight > metrics_.framebufferExtent.height)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The reported soft keyboard occlusion exceeds the window height");
        }
        softKeyboardOccludedPhysicalHeight_ = occludedPhysicalHeight;
        return Core::success();
    }

    [[nodiscard]] float softKeyboardOccludedLogicalHeight() const noexcept override
    {
        // Converted here because this is where the content scale lives; UI code subtracts logical
        // units, not pixels.
        return static_cast<float>(softKeyboardOccludedPhysicalHeight_) / metrics_.contentScale.y;
    }

    [[nodiscard]] AndroidSoftKeyboardRequest takePendingSoftKeyboardRequest() noexcept override
    {
        // Reading clears, so one request produces exactly one InputMethodManager call instead of
        // being re-applied on every frame the host polls.
        return std::exchange(pendingSoftKeyboardRequest_, AndroidSoftKeyboardRequest::None);
    }

    [[nodiscard]] std::optional<AndroidCaretPixels> caretPixels() const noexcept override
    {
        // Deliberately non-consuming, unlike the keyboard request: an IME that asked for cursor updates
        // expects the current caret every time it looks, so clearing on read would leave the second
        // query with nothing.
        return caretPixels_;
    }

    [[nodiscard]] u64 publishedCompositionStartCount() const noexcept override
    {
        return compositionStarts_;
    }

    [[nodiscard]] u64 publishedCompositionUpdateCount() const noexcept override
    {
        return compositionUpdates_;
    }

    [[nodiscard]] u64 publishedCompositionEndCount() const noexcept override
    {
        return compositionEnds_;
    }

    [[nodiscard]] u64 publishedCompositionCancelCount() const noexcept override
    {
        return compositionCancels_;
    }

  private:
    [[nodiscard]] static bool isFinitePositiveCaret(const TextInputCaretRect& caret) noexcept
    {
        return std::isfinite(caret.x) && std::isfinite(caret.y) && std::isfinite(caret.width) &&
               std::isfinite(caret.height) && caret.width >= 0.0 && caret.height > 0.0;
    }

    // Window-logical -> physical pixels, which is the space CursorAnchorInfo works in.
    //
    // Rounded rather than truncated so a caret does not drift half a pixel up-left of where it is
    // drawn, and range-checked because the product of a large logical value and a high density can
    // leave i32 -- at which point a cast would wrap and place the candidate window off-screen.
    [[nodiscard]] std::optional<AndroidCaretPixels> toCaretPixels(const TextInputCaretRect& caret) const noexcept
    {
        const auto convert = [](double logical, float scale) noexcept -> std::optional<i32> {
            const double scaled = std::round(logical * static_cast<double>(scale));
            if (!std::isfinite(scaled) || scaled < static_cast<double>((std::numeric_limits<i32>::min)()) ||
                scaled > static_cast<double>((std::numeric_limits<i32>::max)()))
            {
                return std::nullopt;
            }
            return static_cast<i32>(scaled);
        };
        const auto x = convert(caret.x, metrics_.contentScale.x);
        const auto y = convert(caret.y, metrics_.contentScale.y);
        const auto width = convert(caret.width, metrics_.contentScale.x);
        const auto height = convert(caret.height, metrics_.contentScale.y);
        if (!x || !y || !width || !height)
        {
            return std::nullopt;
        }
        return AndroidCaretPixels{.x = *x, .y = *y, .width = *width, .height = *height};
    }

    [[nodiscard]] Core::Status setSoftKeyboardRequest(AndroidSoftKeyboardRequest request) noexcept
    {
        if (auto status = checkUsable("soft keyboard request"); !status)
        {
            return status;
        }
        pendingSoftKeyboardRequest_ = request;
        return Core::success();
    }

    void releaseAllPointers() noexcept
    {
        for (auto& pointer : pointerState_.pointers)
        {
            pointer.heldButtons.reset();
            pointer.present = false;
        }
        // Keys go with the window for the same reason fingers do: a key held when the activity loses its
        // window will never deliver an Up, so leaving the bit set latches it for the rest of the run.
        pointerState_.heldKeys.reset();
    }

    // Records that the surface facts changed, advancing surfaceRevision at most once per observation.
    //
    // Incrementing on every lifecycle call is wrong, and cost a real debugging round: Android delivers
    // TERM_WINDOW and INIT_WINDOW back to back during a background/foreground cycle, with no poll in
    // between, so two eager increments made the revision jump by 2 and RenderSurfaceStateTracker
    // rejected every subsequent frame with "Render surface revision must advance exactly once for each
    // committed state change" -- the screen simply went black with no other symptom.
    //
    // So the counter tracks *observed* state, not events: further changes before the next snapshot
    // read collapse into the pending one, which is exactly the tracker's contract.
    [[nodiscard]] Core::Status markSurfaceFactsChanged() noexcept
    {
        if (surfaceRevisionPendingObservation_)
        {
            return Core::success();
        }
        if (surfaceSnapshot_.surfaceRevision == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The Android WindowSurface revision is exhausted");
        }
        ++surfaceSnapshot_.surfaceRevision;
        surfaceRevisionPendingObservation_ = true;
        return Core::success();
    }

    // The starting input state, carried across polls and mutated by each drained event.
    //
    // Not a default-constructed WindowInputSnapshot: that one fails PlatformFrameBuilder's
    // window-snapshot validation outright, because the input's window id and metrics revision
    // must match the metrics it accompanies. Discovered by running these tests on an emulator --
    // it compiled fine and every pollFrame() failed.
    //
    // Nor does it leave pointers `present`. That default suits a mouse, which always has a
    // position, and is wrong for touch: between taps a finger has no position at all, so a
    // present-but-idle pointer would latch hover on whatever was last touched (ADR 0032 C2).
    // Absent is the honest steady state, and a Down is what makes a pointer present.
    [[nodiscard]] static WindowInputSnapshot quiescentInput(const WindowMetricsSnapshot& metrics) noexcept
    {
        WindowInputSnapshot input{};
        input.window = metrics.window;
        input.sourceMetricsRevision = metrics.revision;
        for (auto& pointer : input.pointers)
        {
            pointer.present = false;
        }
        return input;
    }

    [[nodiscard]] double toLogicalX(float physicalX) const noexcept
    {
        return static_cast<double>(physicalX) / static_cast<double>(metrics_.contentScale.x);
    }

    [[nodiscard]] double toLogicalY(float physicalY) const noexcept
    {
        return static_cast<double>(physicalY) / static_cast<double>(metrics_.contentScale.y);
    }

    // Drains every queued touch into this frame's transitions, updating the carried pointer state
    // as it goes. Android reports physical pixels; the conversion to window-logical happens here
    // because this is where the content scale is known.
    [[nodiscard]] Core::Status drainTouchEvents() noexcept
    {
        if (touchEvents_ == nullptr)
        {
            return Core::success();
        }

        AndroidTouchEvent event{};
        while (touchEvents_->tryPop(event))
        {
            if (event.pointerSlot >= PointerCapacity)
            {
                // The producer already reports a full slot table by dropping the event, so a bad
                // slot here means a producer bug rather than a busy device. Rejecting keeps it
                // from corrupting an unrelated pointer's state.
                return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                     "An Android touch event carried an out-of-range pointer slot");
            }

            const auto slot = static_cast<PointerId>(event.pointerSlot);
            PointerSnapshot& pointer = pointerState_.pointers[event.pointerSlot];
            const double logicalX = toLogicalX(event.physicalX);
            const double logicalY = toLogicalY(event.physicalY);

            switch (event.action)
            {
            case AndroidTouchAction::Down:
                pointer.present = true;
                pointer.logicalX = logicalX;
                pointer.logicalY = logicalY;
                pointer.heldButtons.set(static_cast<usize>(PointerButton::Primary));
                if (auto status = append(PointerButtonTransition{
                        .window = metrics_.window,
                        .pointer = slot,
                        .button = PointerButton::Primary,
                        .state = DigitalTransition::Down,
                        .logicalX = logicalX,
                        .logicalY = logicalY,
                    });
                    !status)
                {
                    return status;
                }
                break;

            case AndroidTouchAction::Move:
            {
                const double deltaX = logicalX - pointer.logicalX;
                const double deltaY = logicalY - pointer.logicalY;
                pointer.logicalX = logicalX;
                pointer.logicalY = logicalY;
                pointer.accumulatedDeltaX += deltaX;
                pointer.accumulatedDeltaY += deltaY;
                if (auto status = append(PointerMoveTransition{
                        .window = metrics_.window,
                        .pointer = slot,
                        .logicalX = logicalX,
                        .logicalY = logicalY,
                        .deltaX = deltaX,
                        .deltaY = deltaY,
                    });
                    !status)
                {
                    return status;
                }
                break;
            }

            case AndroidTouchAction::Up:
                // Position is kept, not zeroed: the Up's own coordinates are what a tap is judged
                // by, and `present = false` is the single thing that says the finger is gone.
                pointer.logicalX = logicalX;
                pointer.logicalY = logicalY;
                pointer.heldButtons.reset(static_cast<usize>(PointerButton::Primary));
                pointer.present = false;
                if (auto status = append(PointerButtonTransition{
                        .window = metrics_.window,
                        .pointer = slot,
                        .button = PointerButton::Primary,
                        .state = DigitalTransition::Up,
                        .logicalX = logicalX,
                        .logicalY = logicalY,
                    });
                    !status)
                {
                    return status;
                }
                break;

            case AndroidTouchAction::Cancel:
                // Scoped to this one pointer, never the whole window. A window-wide cancel is
                // exactly the defect ADR 0032 cites from cocos2d-x: one finger leaving would make
                // the others drop the controls they are still holding. `pointer` being set is what
                // makes this per-pointer; nullopt would mean all eight.
                pointer.heldButtons.reset();
                pointer.present = false;
                if (auto status = append(InputCancelTransition{
                        .routedWindow = metrics_.window,
                        // FocusLost is the closest existing reason: Android takes the gesture away
                        // when the window stops receiving it. No new enumerator is invented here --
                        // that would be a public contract change for no behavioural difference.
                        .reason = InputCancelReason::FocusLost,
                        .gamepad = std::nullopt,
                        .pointer = slot,
                    });
                    !status)
                {
                    return status;
                }
                break;
            }
        }
        return Core::success();
    }

    // Drains queued key events into this frame's transitions and the carried held-key set.
    [[nodiscard]] Core::Status drainKeyEvents() noexcept
    {
        if (keyEvents_ == nullptr)
        {
            return Core::success();
        }

        AndroidKeyEvent event{};
        while (keyEvents_->tryPop(event))
        {
            const Key key = androidKeyFromKeyCode(event.androidKeyCode);
            if (key == Key::Unknown)
            {
                // Dropped rather than reported as Unknown: publishing it would let a consumer key off
                // an enumerator that means "we could not identify this", which is never actionable.
                continue;
            }

            const auto keyIndex = static_cast<usize>(key);
            const bool down = event.action == AndroidKeyAction::Down;
            // A repeat must not re-set an already-held bit as if it were a new press, and an Up for a
            // key we never saw down must not clear a bit that was never set. Both are silent on
            // desktop and routine on Android, where the OS can deliver an Up after focus changes.
            if (down)
            {
                pointerState_.heldKeys.set(keyIndex);
            }
            else
            {
                pointerState_.heldKeys.reset(keyIndex);
            }

            if (auto status = append(KeyTransition{
                    .window = metrics_.window,
                    .key = key,
                    .state = down ? DigitalTransition::Down : DigitalTransition::Up,
                    // Forwarded rather than dropped: held-key navigation depends on repeats, and
                    // KeyTransition already carries the flag so consumers can ignore it if they want.
                    .repeat = down && event.repeat,
                });
                !status)
            {
                return status;
            }
        }
        return Core::success();
    }

    // Drains committed IME text into this frame's transitions.
    //
    // Unlike keys, text carries no held state: a commit is a one-shot event, so there is nothing in
    // WindowInputSnapshot to update. That also means a dropped commit is simply lost input rather than a
    // stuck state, which is why the queue may be lossy at all.
    [[nodiscard]] Core::Status drainTextEvents() noexcept
    {
        if (textEvents_ == nullptr)
        {
            return Core::success();
        }

        AndroidTextEvent event{};
        while (textEvents_->tryPop(event))
        {
            // `event` outlives the append call, which matters: TextInputTransition holds a borrowed
            // view, and PlatformFrameBuilder copies it into its own arena during append. Passing a view
            // into a temporary here would dangle before the copy happened.
            if (auto status = append(TextInputTransition{
                    .window = metrics_.window,
                    .committedUtf8 = std::string_view{event.utf8.data(), event.byteCount},
                });
                !status)
            {
                return status;
            }
            ++publishedTextCommits_;
        }
        return Core::success();
    }

    // Drains composing-text events, publishing each one's stage and any text it resolved into.
    //
    // Commits travel through this queue too, which is the point: a commit that ends a composing pass
    // must publish Ended *before* its text, and the session is the only thing that knows whether a
    // composition was in flight. Two queues could not express that relationship at all -- they would be
    // drained one after the other, so every commit would land before every stage of the pass that
    // produced it.
    [[nodiscard]] Core::Status drainCompositionEvents() noexcept
    {
        // The window-loss cancel is published here rather than where it happens, because that path runs
        // outside a frame and PlatformFrameBuilder rejects an append with no frame begun. Latching it
        // also collapses repeated lifecycle calls into the one cancel the session actually has to make.
        if (compositionCancelPending_)
        {
            compositionCancelPending_ = false;
            if (auto cancelled = composition_.cancel(); cancelled.has_value())
            {
                if (auto status = publishComposition(*cancelled); !status)
                {
                    return status;
                }
            }
        }
        if (compositionEvents_ == nullptr)
        {
            return Core::success();
        }

        AndroidCompositionEvent event{};
        while (compositionEvents_->tryPop(event))
        {
            const Detail::AndroidCompositionOutcome outcome = composition_.apply(event);
            if (auto status = publishComposition(outcome); !status)
            {
                return status;
            }
        }
        return Core::success();
    }

    // Publishes one outcome: the stage first, then any committed text.
    //
    // `event` in the caller outlives this call, which matters because both views are borrowed --
    // PlatformFrameBuilder copies them into its own arena during append, and the preedit view points
    // into the session rather than the event.
    [[nodiscard]] Core::Status publishComposition(const Detail::AndroidCompositionOutcome& outcome) noexcept
    {
        if (outcome.stage.has_value())
        {
            if (auto status = append(TextCompositionTransition{
                    .window = metrics_.window,
                    .preeditUtf8 = outcome.preeditUtf8,
                    .cursorCodepoint = outcome.cursorCodepoint,
                    .stage = *outcome.stage,
                });
                !status)
            {
                return status;
            }
            switch (*outcome.stage)
            {
            case TextCompositionStage::Started:
                ++compositionStarts_;
                break;
            case TextCompositionStage::Updated:
                ++compositionUpdates_;
                break;
            case TextCompositionStage::Ended:
                ++compositionEnds_;
                break;
            case TextCompositionStage::Cancelled:
                ++compositionCancels_;
                break;
            }
        }
        if (!outcome.committedUtf8.empty())
        {
            if (auto status = append(TextInputTransition{
                    .window = metrics_.window,
                    .committedUtf8 = outcome.committedUtf8,
                });
                !status)
            {
                return status;
            }
            ++publishedTextCommits_;
        }
        return Core::success();
    }

    // Capacity exhaustion is a real failure, not something to swallow: a dropped transition means
    // a press with no matching release downstream.
    [[nodiscard]] Core::Status append(InputTransitionPayload payload) noexcept
    {
        const FrameBatchAppendResult result = frameBuilder_.appendInputTransition(std::move(payload));
        switch (result)
        {
        case FrameBatchAppendResult::Appended:
        case FrameBatchAppendResult::Coalesced:
            return Core::success();
        case FrameBatchAppendResult::RejectedCapacity:
            return Core::failure(PlatformErrorCode::InvalidFrameCapacity,
                                 "The Android touch batch exceeded the frame input transition capacity");
        default:
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "The Android platform backend produced an invalid input transition");
        }
    }

    [[nodiscard]] static Integration::WindowSurfaceSnapshot
    makeSurfaceSnapshot(Integration::WindowSurfaceId surfaceId, const WindowMetricsSnapshot& metrics,
                        u64 surfaceRevision) noexcept
    {
        return Integration::WindowSurfaceSnapshot{
            .surface = surfaceId,
            .sourceWindow = metrics.window,
            .framebufferExtent = metrics.framebufferExtent,
            .contentScale = metrics.contentScale,
            .sourceMetricsRevision = metrics.revision,
            .surfaceRevision = surfaceRevision,
            .suspended = metrics.framebufferExtent.width == 0 || metrics.framebufferExtent.height == 0,
        };
    }

    // One guard for all entry points: every method shares the same stopped/owner-thread
    // preconditions, and stating them once keeps them from drifting apart.
    [[nodiscard]] Core::Status checkUsable(const char* operation) const noexcept
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped,
                                 "The Android platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The Android platform backend must be used on its owner thread");
        }
        (void)operation;
        return Core::success();
    }

    PlatformFrameBuilder frameBuilder_;
    WindowPool windowPool_;
    SurfacePool surfacePool_;
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl_;
    std::shared_ptr<AndroidTouchEventQueue> touchEvents_;
    std::shared_ptr<AndroidKeyEventQueue> keyEvents_;
    std::shared_ptr<AndroidTextEventQueue> textEvents_;
    std::shared_ptr<AndroidCompositionEventQueue> compositionEvents_;
    // Owner-thread only, like every other piece of drained state: it is mutated exclusively while
    // draining, which happens inside pollFrame.
    Detail::AndroidCompositionSession composition_{};
    u64 publishedTextCommits_ = 0;
    u64 compositionStarts_ = 0;
    u64 compositionUpdates_ = 0;
    u64 compositionEnds_ = 0;
    u64 compositionCancels_ = 0;
    // Set when the window died with a composition in flight, cleared by the poll that publishes the
    // cancel. See drainCompositionEvents for why it cannot be published at the point it happens.
    bool compositionCancelPending_ = false;
    std::optional<AndroidCaretPixels> caretPixels_{};
    u32 softKeyboardOccludedPhysicalHeight_ = 0;
    AndroidSoftKeyboardRequest pendingSoftKeyboardRequest_ = AndroidSoftKeyboardRequest::None;
    Integration::WindowSurfaceId surfaceId_{};
    std::uintptr_t nativeWindow_ = 0;
    WindowMetricsSnapshot metrics_{};
    // Carried across polls: a finger stays down between frames, so the snapshot is state rather
    // than something rebuilt per poll.
    WindowInputSnapshot pointerState_{};
    Integration::WindowSurfaceSnapshot surfaceSnapshot_{};
    // True between a surface-facts change and the poll that publishes it, so back-to-back lifecycle
    // events collapse into one revision step. See markSurfaceFactsChanged for why that matters.
    bool surfaceRevisionPendingObservation_ = false;
    std::thread::id ownerThread_{};
    u64 nextFrameId_ = 1;
    bool stopped_ = false;
    bool leaseAcquired_ = false;
    bool published_ = false;
};

} // namespace

Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createAndroidWindowSurfacePlatformBackend(const AndroidPlatformBackendCreateParams& params)
{
    if (auto status = validateWindowGeometry(params.window, params.framebufferExtent, params.contentScale);
        !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    auto frameBuilder = PlatformFrameBuilder::Create(params.platform.frameCapacities);
    if (!frameBuilder)
    {
        return std::unexpected(std::move(frameBuilder.error()));
    }

    // Both pools are created before the metrics they identify, and both must outlive their ids,
    // so ownership moves into the backend below. Capacity 1: one activity, one native window.
    auto windowPool = WindowPool::Create(1);
    if (!windowPool)
    {
        return std::unexpected(std::move(windowPool.error()));
    }
    auto windowId = windowPool->tryEmplace(AndroidWindowRecord{});
    if (!windowId)
    {
        return std::unexpected(std::move(windowId.error()));
    }
    auto surfacePool = SurfacePool::Create(1);
    if (!surfacePool)
    {
        return std::unexpected(std::move(surfacePool.error()));
    }
    auto surfaceId = surfacePool->tryEmplace(AndroidWindowSurfaceRecord{});
    if (!surfaceId)
    {
        return std::unexpected(std::move(surfaceId.error()));
    }

    const WindowMetricsSnapshot metrics =
        makeWindowMetrics(*windowId, params.framebufferExtent, params.contentScale, 1);

    try
    {
        auto leaseControl = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
        leaseControl->ownerThread = std::this_thread::get_id();

        return std::unique_ptr<Integration::IWindowSurfacePlatformBackend>{
            std::make_unique<AndroidWindowSurfacePlatformBackend>(
                std::move(*frameBuilder), std::move(*windowPool), std::move(*surfacePool), std::move(leaseControl),
                params.touchEvents, params.keyEvents, params.textEvents, params.compositionEvents, *surfaceId,
                params.window.nativeWindow, metrics)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "The Android platform backend allocation failed");
    }
}

} // namespace Tina::Platform
