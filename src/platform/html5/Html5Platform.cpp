#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/html5/Html5PlatformFactory.hpp>

#include "Html5KeyTranslation.hpp"
#include "Html5TouchSlotTable.hpp"

#include "WindowSurfaceLeaseAccess.hpp"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace Tina::Platform {
namespace {

struct Html5WindowRecord final {};
using WindowPool = Core::GenerationPool<Html5WindowRecord, WindowRegistryTag>;

struct Html5WindowSurfaceRecord final {};
using SurfacePool = Core::GenerationPool<Html5WindowSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

// Browser callbacks fire between polls, so each one only records what happened and
// pollFrame() replays the queue. Doing the frame assembly inside the callback is not
// possible: a frame must be built between one beginFrame/finishFrame pair, and the
// browser decides when callbacks run.
struct QueuedKey final {
    Key key;
    DigitalTransition state;
    bool repeat;
};

struct QueuedPointerMove final {
    double logicalX;
    double logicalY;
    double deltaX;
    double deltaY;
};

struct QueuedPointerButton final {
    PointerButton button;
    DigitalTransition state;
    double logicalX;
    double logicalY;
};

struct QueuedWheel final {
    double deltaX;
    double deltaY;
    double logicalX;
    double logicalY;
};

// The browser stops sending keyup once the canvas loses focus, so every held key and
// button has to be released here or it stays down forever.
struct QueuedFocusLost final {};

struct QueuedResize final {
    u32 logicalWidth;
    u32 logicalHeight;
    u32 framebufferWidth;
    u32 framebufferHeight;
    float scaleX;
    float scaleY;
};

// Pointer lock ending is not something the page can refuse: Escape always works. The
// backend has to observe it rather than assume its own requested mode is still live.
struct QueuedPointerLockChanged final {
    bool locked;
};

// One finger's worth of change. The slot is resolved in the callback, so neither the DOM
// identifier nor the slot table ever enters the replay path.
struct QueuedTouch final {
    PointerId slot;
    double logicalX;
    double logicalY;
    double deltaX;
    double deltaY;
    // touchmove only moves the finger; it must not disturb heldButtons, or the Down/Up pairing
    // the frame builder cross-checks would stop balancing.
    bool moveOnly;
    // Down for touchstart, Up for touchend and touchcancel.
    DigitalTransition state;
    // touchend/touchcancel: the finger is gone, so the slot has to stop being present. A
    // touch has no position between taps, and leaving present set latches hover on the last
    // tap forever (ADR 0032 C2).
    bool released;
};

using QueuedEvent = std::variant<QueuedKey, QueuedPointerMove, QueuedPointerButton, QueuedWheel, QueuedFocusLost,
                                 QueuedResize, QueuedPointerLockChanged, QueuedTouch>;

[[nodiscard]] PointerButton pointerButtonFromDomButton(unsigned short domButton) noexcept
{
    switch (domButton)
    {
    case 0:
        return PointerButton::Primary;
    case 1:
        return PointerButton::Middle;
    case 2:
        return PointerButton::Secondary;
    default:
        return PointerButton::Count;
    }
}

class Html5PlatformBackend final : public Integration::IWindowSurfacePlatformBackend {
  public:
    Html5PlatformBackend(PlatformFrameBuilder frameBuilder, WindowPool windowPool, SurfacePool surfacePool,
                         Integration::WindowSurfaceId surfaceId,
                         std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> surfaceLeaseControl,
                         std::string canvasSelector, WindowMetricsSnapshot metrics, u32 queuedEventLimit) noexcept
        : frameBuilder_(std::move(frameBuilder)), windowPool_(std::move(windowPool)),
          surfacePool_(std::move(surfacePool)), surfaceId_(surfaceId),
          surfaceLeaseControl_(std::move(surfaceLeaseControl)), canvasSelector_(std::move(canvasSelector)),
          metrics_(metrics), surfaceSnapshot_(makeSurfaceSnapshot(surfaceId, metrics, 1)),
          queuedEventLimit_(queuedEventLimit), ownerThread_(std::this_thread::get_id())
    {
        input_.window = metrics_.window;
        input_.sourceMetricsRevision = metrics_.revision;
    }

    Html5PlatformBackend(const Html5PlatformBackend&) = delete;
    Html5PlatformBackend& operator=(const Html5PlatformBackend&) = delete;
    Html5PlatformBackend(Html5PlatformBackend&&) = delete;
    Html5PlatformBackend& operator=(Html5PlatformBackend&&) = delete;

    ~Html5PlatformBackend() override
    {
        unregisterCallbacks();
    }

    // Called once after construction, so the callbacks capture a pointer that stays
    // valid: the backend is heap-allocated and never moves.
    void registerCallbacks() noexcept;
    void unregisterCallbacks() noexcept;

    void enqueue(QueuedEvent event) noexcept
    {
        // A browser can deliver events faster than the game polls, and an unbounded
        // queue would grow until the tab dies. Dropping to a reset marker instead
        // tells downstream input state to resynchronise rather than silently
        // desynchronising, which is what dropping individual events would do.
        if (queuedEvents_.size() >= queuedEventLimit_)
        {
            queueOverflowed_ = true;
            return;
        }
        queuedEvents_.push_back(std::move(event));
    }

    void requestExit() noexcept
    {
        exitRequested_ = true;
    }

    // Resolves one changed touch point to a slot and queues it. Kept here rather than in the
    // callback so the slot table stays private and the identifier never reaches the queue.
    void enqueueTouchPoint(int eventType, const EmscriptenTouchPoint& point) noexcept
    {
        const bool isStart = eventType == EMSCRIPTEN_EVENT_TOUCHSTART;
        const bool isEnd = eventType == EMSCRIPTEN_EVENT_TOUCHEND || eventType == EMSCRIPTEN_EVENT_TOUCHCANCEL;

        const PointerId slot = isStart ? touchSlots_.acquire(point.identifier) : touchSlots_.find(point.identifier);
        if (slot == Html5TouchSlotTable::InvalidSlot)
        {
            // Either more fingers than PointerCapacity, or a move/end whose start was lost to a
            // queue overflow. Inventing a Down for a finger never seen is worse than dropping
            // this event: the overflow already published an InputStreamReset telling downstream
            // state to resynchronise. An untracked identifier holds no slot, so nothing leaks.
            return;
        }

        const auto logicalX = static_cast<double>(point.targetX);
        const auto logicalY = static_cast<double>(point.targetY);

        // The browser reports no movement delta for a touch point, so it is differenced against
        // the last published position. A start has no predecessor and must report zero, or the
        // first frame of every gesture carries a jump from wherever the previous finger was.
        double deltaX = 0.0;
        double deltaY = 0.0;
        if (!isStart)
        {
            double previousX = 0.0;
            double previousY = 0.0;
            if (touchSlots_.lastPosition(slot, previousX, previousY))
            {
                deltaX = logicalX - previousX;
                deltaY = logicalY - previousY;
            }
        }
        touchSlots_.setLastPosition(slot, logicalX, logicalY);

        enqueue(QueuedTouch{
            .slot = slot,
            .logicalX = logicalX,
            .logicalY = logicalY,
            .deltaX = deltaX,
            .deltaY = deltaY,
            .moveOnly = eventType == EMSCRIPTEN_EVENT_TOUCHMOVE,
            .state = isEnd ? DigitalTransition::Up : DigitalTransition::Down,
            .released = isEnd,
        });

        if (isEnd)
        {
            // Released after queueing, so the delta above still had the mapping available.
            touchSlots_.release(point.identifier);
        }
    }

    [[nodiscard]] const std::string& canvasSelector() const noexcept
    {
        return canvasSelector_;
    }

    [[nodiscard]] Core::Result<Integration::NativeWindowSurfaceLease>
    acquirePrimaryWindowSurfaceLease() noexcept override
    {
        if (stopped_ || surfaceLeaseControl_ == nullptr || !surfaceId_.hasValue())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The HTML5 primary WindowSurface is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The HTML5 WindowSurface lease must be acquired on the creating thread");
        }
        if (surfaceLeaseAcquired_)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired,
                                 "The HTML5 primary WindowSurface lease was already acquired");
        }

        // The canvas has no handle to read: bgfx's HTML5 context takes the CSS selector
        // through nwh. canvasSelector_ outlives every lease because the backend owns it and
        // never reassigns it, which is what makes handing out its data() pointer sound.
        auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
            surfaceLeaseControl_, surfaceId_,
            Integration::Detail::NativeWindowBinding{
                .kind = Integration::Detail::NativeWindowBindingKind::Html5,
                .nativeDisplay = 0,
                .nativeWindow = reinterpret_cast<std::uintptr_t>(canvasSelector_.c_str()),
                .bindingRevision = 1,
            });
        if (!lease)
        {
            return std::unexpected(std::move(lease.error()));
        }
        surfaceLeaseAcquired_ = true;
        return std::move(*lease);
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    primaryWindowSurfaceSnapshot() const noexcept override
    {
        if (stopped_ || !surfaceSnapshot_.surface.hasValue())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The HTML5 primary WindowSurface snapshot is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The HTML5 WindowSurface snapshot must be read on the creating thread");
        }
        return surfaceSnapshot_;
    }

    [[nodiscard]] Core::Status publishPrimaryWindow() noexcept override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The HTML5 platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The HTML5 primary window must be published on the creating thread");
        }
        // A canvas is already in the page and already visible; there is no equivalent of
        // showing a hidden window. The call stays idempotent so the host sequence is the
        // same shape on every backend.
        windowPublished_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() override;
    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame() override;
    Core::Status updateTextInputPlacement(std::optional<TextInputPlacement> placement) override;
    Core::Status setPointerCaptureMode(PointerCaptureMode mode) override;
    void shutdown() noexcept override;

  private:
    [[nodiscard]] Core::Status verifyOwnerThread() const noexcept
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The HTML5 platform backend was used from a non-owner thread");
        }
        return Core::success();
    }

    // Clears every held key and button and reports one cancel, so no press outlives
    // the focus that produced it.
    [[nodiscard]] FrameBatchAppendResult cancelAllHeldInput(InputCancelReason reason) noexcept
    {
        input_.heldKeys.reset();
        for (auto& pointer : input_.pointers)
        {
            pointer.heldButtons.reset();
        }
        releaseAllTouchSlots();
        return frameBuilder_.appendInputTransition(InputCancelTransition{
            .routedWindow = metrics_.window,
            .reason = reason,
        });
    }

    // The browser stops delivering touchend once the canvas loses focus, and a dropped
    // touchend after a queue overflow has the same effect, so both paths have to let the
    // fingers go here or their slots stay claimed for the rest of the run.
    //
    // Only slots a touch actually owns become absent: the mouse shares slot 0, and losing
    // focus does not mean the cursor left the canvas.
    void releaseAllTouchSlots() noexcept
    {
        for (usize slot = 0; slot < PointerCapacity; ++slot)
        {
            if (touchSlots_.ownsSlot(static_cast<PointerId>(slot)))
            {
                input_.pointers[slot].present = false;
            }
        }
        touchSlots_.releaseAll();
    }

    [[nodiscard]] static Integration::WindowSurfaceSnapshot makeSurfaceSnapshot(Integration::WindowSurfaceId surfaceId,
                                                                               const WindowMetricsSnapshot& metrics,
                                                                               u64 surfaceRevision) noexcept
    {
        return Integration::WindowSurfaceSnapshot{
            .surface = surfaceId,
            .sourceWindow = metrics.window,
            .framebufferExtent = metrics.framebufferExtent,
            .contentScale = metrics.contentScale,
            .sourceMetricsRevision = metrics.revision,
            .surfaceRevision = surfaceRevision,
            // A browser tab has no minimised state the canvas can observe; a hidden tab keeps
            // its size and simply stops getting frames. Only a zero-sized canvas suspends.
            .suspended = metrics.framebufferExtent.width == 0 || metrics.framebufferExtent.height == 0,
        };
    }

    // Recomputes the surface snapshot from committed metrics, advancing surfaceRevision by
    // exactly one when a surface fact changed and leaving it alone otherwise. The host
    // validates both halves of that rule every frame, so the two must be decided together.
    [[nodiscard]] Core::Status refreshSurfaceSnapshot() noexcept
    {
        Integration::WindowSurfaceSnapshot next =
            makeSurfaceSnapshot(surfaceId_, metrics_, surfaceSnapshot_.surfaceRevision);
        const bool changed = next.framebufferExtent != surfaceSnapshot_.framebufferExtent ||
                             next.contentScale != surfaceSnapshot_.contentScale ||
                             next.suspended != surfaceSnapshot_.suspended;
        if (changed)
        {
            if (surfaceSnapshot_.surfaceRevision == (std::numeric_limits<u64>::max)())
            {
                return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                     "The HTML5 WindowSurface revision is exhausted");
            }
            next.surfaceRevision = surfaceSnapshot_.surfaceRevision + 1;
        }
        surfaceSnapshot_ = next;
        return Core::success();
    }

    PlatformFrameBuilder frameBuilder_;
    WindowPool windowPool_;
    SurfacePool surfacePool_;
    Integration::WindowSurfaceId surfaceId_{};
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> surfaceLeaseControl_;
    std::string canvasSelector_;
    WindowMetricsSnapshot metrics_{};
    Integration::WindowSurfaceSnapshot surfaceSnapshot_{};
    WindowInputSnapshot input_{};
    std::deque<QueuedEvent> queuedEvents_{};
    Html5TouchSlotTable touchSlots_{};
    u32 queuedEventLimit_ = 0;
    u64 nextFrameId_ = 1;
    std::thread::id ownerThread_{};
    PointerCaptureMode requestedCapture_ = PointerCaptureMode::Free;
    bool pointerLockActive_ = false;
    // Set when a Locked request is waiting for the user gesture the browser requires.
    bool pointerLockPending_ = false;
    // Drops the one synthetic jump the browser reports as the lock engages.
    bool dropNextPointerDelta_ = false;
    bool queueOverflowed_ = false;
    bool exitRequested_ = false;
    bool callbacksRegistered_ = false;
    bool surfaceLeaseAcquired_ = false;
    bool windowPublished_ = false;
    bool stopped_ = false;
};

Core::Result<std::optional<WindowMetricsSnapshot>> Html5PlatformBackend::initialPrimaryWindowMetrics()
{
    if (auto owner = verifyOwnerThread(); !owner.has_value())
    {
        return std::unexpected(std::move(owner.error()));
    }
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped, "The HTML5 platform backend is stopped");
    }
    // A canvas always exists here, so this backend is never the nullopt Headless case.
    return std::optional<WindowMetricsSnapshot>{metrics_};
}

Core::Result<PlatformPollResult> Html5PlatformBackend::pollFrame()
{
    if (auto owner = verifyOwnerThread(); !owner.has_value())
    {
        return std::unexpected(std::move(owner.error()));
    }
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped, "The HTML5 platform backend is stopped");
    }
    if (exitRequested_)
    {
        return PlatformPollResult::Exit();
    }
    if (nextFrameId_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(PlatformErrorCode::FrameSequenceExhausted,
                             "The HTML5 platform frame sequence is exhausted");
    }

    auto beginStatus = frameBuilder_.beginFrame(PlatformFrameId{nextFrameId_++});
    if (!beginStatus.has_value())
    {
        return std::unexpected(std::move(beginStatus.error()));
    }

    // Deltas are per-poll: they describe movement since the previous frame, so they
    // start at zero and accumulate only what this poll drains.
    for (auto& pointer : input_.pointers)
    {
        pointer.accumulatedDeltaX = 0.0;
        pointer.accumulatedDeltaY = 0.0;
    }

    if (queueOverflowed_)
    {
        queueOverflowed_ = false;
        queuedEvents_.clear();
        const FrameBatchAppendResult result = frameBuilder_.appendInputTransition(InputStreamReset{
            .routedWindow = metrics_.window,
            .reason = InputResetReason::CapacityExceeded,
        });
        if (result == FrameBatchAppendResult::InvalidPayload || result == FrameBatchAppendResult::SequenceExhausted)
        {
            (void)frameBuilder_.discardFrame();
            return Core::failure(PlatformErrorCode::CallbackFrameAssemblyFailed,
                                 "The HTML5 backend could not publish a queue overflow reset");
        }
        // Held state is no longer trustworthy once events were dropped.
        input_.heldKeys.reset();
        for (auto& pointer : input_.pointers)
        {
            pointer.heldButtons.reset();
        }
        releaseAllTouchSlots();
    }

    bool metricsChanged = false;
    while (!queuedEvents_.empty())
    {
        const QueuedEvent event = std::move(queuedEvents_.front());
        queuedEvents_.pop_front();

        if (const auto* key = std::get_if<QueuedKey>(&event); key != nullptr)
        {
            input_.heldKeys.set(static_cast<usize>(key->key), key->state == DigitalTransition::Down);
            (void)frameBuilder_.appendInputTransition(KeyTransition{
                .window = metrics_.window,
                .key = key->key,
                .state = key->state,
                .repeat = key->repeat,
            });
            continue;
        }
        if (const auto* move = std::get_if<QueuedPointerMove>(&event); move != nullptr)
        {
            // The browser warps the cursor as it captures it and reports that warp as
            // movement. Publishing it would snap a first-person camera on the first
            // frame after the lock engages, so the position is taken and the delta
            // dropped exactly once.
            double deltaX = move->deltaX;
            double deltaY = move->deltaY;
            if (dropNextPointerDelta_)
            {
                dropNextPointerDelta_ = false;
                deltaX = 0.0;
                deltaY = 0.0;
            }

            PointerSnapshot& pointer = input_.pointers[PrimaryPointerId];
            pointer.present = true;
            pointer.logicalX = move->logicalX;
            pointer.logicalY = move->logicalY;
            pointer.accumulatedDeltaX += deltaX;
            pointer.accumulatedDeltaY += deltaY;
            (void)frameBuilder_.appendInputTransition(PointerMoveTransition{
                .window = metrics_.window,
                .pointer = PrimaryPointerId,
                .logicalX = move->logicalX,
                .logicalY = move->logicalY,
                .deltaX = deltaX,
                .deltaY = deltaY,
            });
            continue;
        }
        if (const auto* button = std::get_if<QueuedPointerButton>(&event); button != nullptr)
        {
            PointerSnapshot& pointer = input_.pointers[PrimaryPointerId];
            pointer.present = true;
            pointer.logicalX = button->logicalX;
            pointer.logicalY = button->logicalY;
            pointer.heldButtons.set(static_cast<usize>(button->button), button->state == DigitalTransition::Down);
            (void)frameBuilder_.appendInputTransition(PointerButtonTransition{
                .window = metrics_.window,
                .pointer = PrimaryPointerId,
                .button = button->button,
                .state = button->state,
                .logicalX = button->logicalX,
                .logicalY = button->logicalY,
            });
            continue;
        }
        if (const auto* wheel = std::get_if<QueuedWheel>(&event); wheel != nullptr)
        {
            (void)frameBuilder_.appendInputTransition(PointerWheelTransition{
                .window = metrics_.window,
                .pointer = PrimaryPointerId,
                .deltaX = wheel->deltaX,
                .deltaY = wheel->deltaY,
                .logicalX = wheel->logicalX,
                .logicalY = wheel->logicalY,
            });
            continue;
        }
        if (const auto* touch = std::get_if<QueuedTouch>(&event); touch != nullptr)
        {
            PointerSnapshot& pointer = input_.pointers[touch->slot];
            pointer.logicalX = touch->logicalX;
            pointer.logicalY = touch->logicalY;
            if (touch->moveOnly)
            {
                pointer.accumulatedDeltaX += touch->deltaX;
                pointer.accumulatedDeltaY += touch->deltaY;
                (void)frameBuilder_.appendInputTransition(PointerMoveTransition{
                    .window = metrics_.window,
                    .pointer = touch->slot,
                    .logicalX = touch->logicalX,
                    .logicalY = touch->logicalY,
                    .deltaX = touch->deltaX,
                    .deltaY = touch->deltaY,
                });
            } else
            {
                // A touch reports contact, not which button: Primary is the only meaningful
                // mapping, and it is what the UI hit-tests for a tap.
                pointer.heldButtons.set(static_cast<usize>(PointerButton::Primary),
                                        touch->state == DigitalTransition::Down);
                (void)frameBuilder_.appendInputTransition(PointerButtonTransition{
                    .window = metrics_.window,
                    .pointer = touch->slot,
                    .button = PointerButton::Primary,
                    .state = touch->state,
                    .logicalX = touch->logicalX,
                    .logicalY = touch->logicalY,
                });
            }
            // After the button state, never before: an absent pointer holding a button fails
            // the per-slot snapshot validation and the whole frame is rejected.
            pointer.present = !touch->released;
            continue;
        }
        if (std::holds_alternative<QueuedFocusLost>(event))
        {
            metrics_.focused = false;
            metricsChanged = true;
            (void)cancelAllHeldInput(InputCancelReason::FocusLost);
            continue;
        }
        if (const auto* resize = std::get_if<QueuedResize>(&event); resize != nullptr)
        {
            metrics_.logicalExtent = LogicalExtent{resize->logicalWidth, resize->logicalHeight};
            metrics_.framebufferExtent = FramebufferExtent{resize->framebufferWidth, resize->framebufferHeight};
            metrics_.contentScale = ContentScale{resize->scaleX, resize->scaleY};
            metricsChanged = true;
            continue;
        }
        if (const auto* lock = std::get_if<QueuedPointerLockChanged>(&event); lock != nullptr)
        {
            pointerLockActive_ = lock->locked;
            if (lock->locked)
            {
                pointerLockPending_ = false;
                dropNextPointerDelta_ = true;
            } else
            {
                // The user pressed Escape. The requested mode is no longer in effect and
                // the page cannot silently reinstate it, so stop claiming it is Locked.
                requestedCapture_ = PointerCaptureMode::Free;
            }
            continue;
        }
    }

    // The revision has to advance before the event that names it, because the frame
    // builder rejects a WindowMetricsChangedEvent whose revision is not exactly the
    // one on the final snapshot.
    if (metricsChanged)
    {
        if (metrics_.revision == (std::numeric_limits<u64>::max)())
        {
            (void)frameBuilder_.discardFrame();
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The HTML5 primary window metrics revision is exhausted");
        }
        ++metrics_.revision;
        (void)frameBuilder_.appendPlatformEvent(WindowMetricsChangedEvent{
            .window = metrics_.window,
            .metricsRevision = metrics_.revision,
        });
    }

    // After the metrics revision advances, never before: a surface fact that changed must be
    // published together with the new source revision that explains it, and the host rejects
    // the frame if the snapshot still names the previous one.
    if (auto surface = refreshSurfaceSnapshot(); !surface.has_value())
    {
        (void)frameBuilder_.discardFrame();
        return Core::failure(std::move(surface.error()));
    }

    input_.window = metrics_.window;
    input_.sourceMetricsRevision = metrics_.revision;
    if (!frameBuilder_.setPrimaryWindowSnapshot(metrics_, input_))
    {
        (void)frameBuilder_.discardFrame();
        return Core::failure(PlatformErrorCode::InvalidFrameSnapshot,
                             "The HTML5 backend could not publish its primary window snapshot");
    }

    auto frame = frameBuilder_.finishFrame();
    if (!frame.has_value())
    {
        return std::unexpected(std::move(frame.error()));
    }
    return PlatformPollResult::Continue(*frame);
}

Core::Status Html5PlatformBackend::updateTextInputPlacement(std::optional<TextInputPlacement> placement)
{
    if (auto owner = verifyOwnerThread(); !owner.has_value())
    {
        return owner;
    }
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped, "The HTML5 platform backend is stopped");
    }
    // A canvas has no IME surface to position. Placing a hidden input element over the
    // caret is the usual browser trick, and this backend does not do it yet, so the
    // placement is accepted and ignored as the contract permits rather than failed:
    // failing would break every TextEdit, while ignoring only costs IME candidate
    // window placement. Clearing is likewise a no-op.
    (void)placement;
    return Core::success();
}

Core::Status Html5PlatformBackend::setPointerCaptureMode(PointerCaptureMode mode)
{
    if (auto owner = verifyOwnerThread(); !owner.has_value())
    {
        return owner;
    }
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped, "The HTML5 platform backend is stopped");
    }

    if (mode == PointerCaptureMode::Free)
    {
        requestedCapture_ = PointerCaptureMode::Free;
        pointerLockPending_ = false;
        if (pointerLockActive_)
        {
            emscripten_exit_pointerlock();
        }
        return Core::success();
    }

    requestedCapture_ = PointerCaptureMode::Locked;
    if (pointerLockActive_)
    {
        return Core::success();
    }
    // A browser only grants pointer lock from inside a user gesture, so this cannot
    // lock synchronously the way a desktop backend does. Deferring asks the browser to
    // engage on the next click instead of failing the call, which is the only way a
    // first-person game on the web can work at all.
    //
    // The caller is not misled: the lock is genuinely not active yet, and until it is
    // the pointer keeps reporting Free-mode deltas. A game that needs to know can see
    // it in the pointer stream, which stays bounded until the lock engages.
    pointerLockPending_ = true;
    const EMSCRIPTEN_RESULT result = emscripten_request_pointerlock(canvasSelector_.c_str(), EM_TRUE);
    if (result != EMSCRIPTEN_RESULT_SUCCESS && result != EMSCRIPTEN_RESULT_DEFERRED)
    {
        pointerLockPending_ = false;
        requestedCapture_ = PointerCaptureMode::Free;
        return Core::failure(PlatformErrorCode::BackendOperationFailed,
                             "The browser refused a pointer lock request for the Tina canvas");
    }
    return Core::success();
}

void Html5PlatformBackend::shutdown() noexcept
{
    if (stopped_)
    {
        return;
    }
    // A live lease still points at canvasSelector_ and at this backend's surface record.
    // Returning here would let the render device keep drawing into a destroyed surface, so
    // the contract violation is fatal rather than silently tolerated.
    if (surfaceLeaseControl_ != nullptr && surfaceLeaseControl_->activeLeaseCount != 0)
    {
        std::terminate();
    }
    unregisterCallbacks();
    if (pointerLockActive_)
    {
        emscripten_exit_pointerlock();
        pointerLockActive_ = false;
    }
    queuedEvents_.clear();
    if (surfaceLeaseControl_ != nullptr)
    {
        surfaceLeaseControl_->surfaceAlive = false;
    }
    (void)windowPool_.erase(metrics_.window);
    if (surfaceId_.hasValue())
    {
        (void)surfacePool_.erase(surfaceId_);
        surfaceId_ = {};
    }
    stopped_ = true;
}

[[nodiscard]] Html5PlatformBackend* backendFrom(void* userData) noexcept
{
    return static_cast<Html5PlatformBackend*>(userData);
}

EM_BOOL onKey(int eventType, const EmscriptenKeyboardEvent* event, void* userData) noexcept
{
    auto* backend = backendFrom(userData);
    const Key key = html5KeyFromDomCode(event->code);
    if (key == Key::Unknown)
    {
        // Not consumed: a key Tina has no slot for should keep its browser behaviour.
        return EM_FALSE;
    }
    backend->enqueue(QueuedKey{
        .key = key,
        .state = eventType == EMSCRIPTEN_EVENT_KEYDOWN ? DigitalTransition::Down : DigitalTransition::Up,
        .repeat = event->repeat != 0,
    });
    // Consumed so the browser does not scroll the page on Space or move focus on Tab
    // while the game owns the keyboard.
    return EM_TRUE;
}

EM_BOOL onMouseMove(int /*eventType*/, const EmscriptenMouseEvent* event, void* userData) noexcept
{
    backendFrom(userData)->enqueue(QueuedPointerMove{
        .logicalX = static_cast<double>(event->targetX),
        .logicalY = static_cast<double>(event->targetY),
        .deltaX = static_cast<double>(event->movementX),
        .deltaY = static_cast<double>(event->movementY),
    });
    return EM_TRUE;
}

EM_BOOL onMouseButton(int eventType, const EmscriptenMouseEvent* event, void* userData) noexcept
{
    const PointerButton button = pointerButtonFromDomButton(event->button);
    if (button == PointerButton::Count)
    {
        return EM_FALSE;
    }
    backendFrom(userData)->enqueue(QueuedPointerButton{
        .button = button,
        .state = eventType == EMSCRIPTEN_EVENT_MOUSEDOWN ? DigitalTransition::Down : DigitalTransition::Up,
        .logicalX = static_cast<double>(event->targetX),
        .logicalY = static_cast<double>(event->targetY),
    });
    return EM_TRUE;
}

EM_BOOL onWheel(int /*eventType*/, const EmscriptenWheelEvent* event, void* userData) noexcept
{
    // Browser wheel deltas are in the units the deltaMode field names, and only
    // DOM_DELTA_PIXEL is a length. Normalising to notches keeps the value comparable
    // with the desktop backends, whose wheel is counted in notches.
    constexpr double PixelsPerNotch = 100.0;
    constexpr double LinesPerNotch = 3.0;
    double scale = 1.0;
    switch (event->deltaMode)
    {
    case DOM_DELTA_PIXEL:
        scale = 1.0 / PixelsPerNotch;
        break;
    case DOM_DELTA_LINE:
        scale = 1.0 / LinesPerNotch;
        break;
    default:
        // DOM_DELTA_PAGE is already coarser than a notch.
        scale = 1.0;
        break;
    }
    backendFrom(userData)->enqueue(QueuedWheel{
        // Browser Y grows downward while a wheel notch toward the user is negative
        // there; negating matches the desktop convention of up being positive.
        .deltaX = event->deltaX * scale,
        .deltaY = -event->deltaY * scale,
        .logicalX = static_cast<double>(event->mouse.targetX),
        .logicalY = static_cast<double>(event->mouse.targetY),
    });
    return EM_TRUE;
}

EM_BOOL onTouch(int eventType, const EmscriptenTouchEvent* event, void* userData) noexcept
{
    auto* backend = backendFrom(userData);
    // touches[] carries every active finger, not just the ones this event is about, so the
    // isChanged flag is what separates them. Replaying an unchanged finger would publish a
    // zero-delta move for every other finger on every event.
    const int touchCount = event->numTouches < 0 ? 0 : event->numTouches;
    const int limit = touchCount < 32 ? touchCount : 32;
    for (int index = 0; index < limit; ++index)
    {
        const EmscriptenTouchPoint& point = event->touches[index];
        if (!point.isChanged)
        {
            continue;
        }
        backend->enqueueTouchPoint(eventType, point);
    }
    // Consumed, which is what stops the browser scrolling the page, pinch-zooming, and -- the
    // one that matters here -- synthesising compatibility mouse events for the touch. Without
    // that, one tap would drive both the touch path and the mouse path into slot 0.
    return EM_TRUE;
}

EM_BOOL onBlur(int /*eventType*/, const EmscriptenFocusEvent* /*event*/, void* userData) noexcept
{
    backendFrom(userData)->enqueue(QueuedFocusLost{});
    return EM_FALSE;
}

EM_BOOL onPointerLockChange(int /*eventType*/, const EmscriptenPointerlockChangeEvent* event, void* userData) noexcept
{
    backendFrom(userData)->enqueue(QueuedPointerLockChanged{.locked = event->isActive != 0});
    return EM_FALSE;
}

void Html5PlatformBackend::registerCallbacks() noexcept
{
    if (callbacksRegistered_)
    {
        return;
    }
    const char* target = canvasSelector_.c_str();
    // useCapture false, and the canvas rather than the window, so a host page that
    // embeds Tina keeps its own event handling outside the canvas.
    emscripten_set_keydown_callback(target, this, EM_FALSE, onKey);
    emscripten_set_keyup_callback(target, this, EM_FALSE, onKey);
    emscripten_set_mousemove_callback(target, this, EM_FALSE, onMouseMove);
    emscripten_set_mousedown_callback(target, this, EM_FALSE, onMouseButton);
    emscripten_set_mouseup_callback(target, this, EM_FALSE, onMouseButton);
    emscripten_set_wheel_callback(target, this, EM_FALSE, onWheel);
    emscripten_set_touchstart_callback(target, this, EM_FALSE, onTouch);
    emscripten_set_touchmove_callback(target, this, EM_FALSE, onTouch);
    emscripten_set_touchend_callback(target, this, EM_FALSE, onTouch);
    emscripten_set_touchcancel_callback(target, this, EM_FALSE, onTouch);
    emscripten_set_blur_callback(target, this, EM_FALSE, onBlur);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, EM_FALSE, onPointerLockChange);
    callbacksRegistered_ = true;
}

void Html5PlatformBackend::unregisterCallbacks() noexcept
{
    if (!callbacksRegistered_)
    {
        return;
    }
    const char* target = canvasSelector_.c_str();
    // Passing a null handler is how Emscripten detaches. This must happen before the
    // backend dies or a later event would call into freed memory.
    emscripten_set_keydown_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_keyup_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_mousemove_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_mousedown_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_mouseup_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_wheel_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_touchstart_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_touchmove_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_touchend_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_touchcancel_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_blur_callback(target, nullptr, EM_FALSE, nullptr);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_FALSE, nullptr);
    callbacksRegistered_ = false;
}

Core::Result<std::unique_ptr<Html5PlatformBackend>> createHtml5Backend(const Html5PlatformCreateParams& params)
{
    if (params.canvasSelector.empty())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "An HTML5 canvas selector must not be empty");
    }

    auto frameBuilder = PlatformFrameBuilder::Create(params.base.frameCapacities);
    if (!frameBuilder.has_value())
    {
        return std::unexpected(std::move(frameBuilder.error()));
    }

    // Capacity 1: a browser tab has one canvas this backend owns.
    auto windowPool = WindowPool::Create(1);
    if (!windowPool.has_value())
    {
        return std::unexpected(std::move(windowPool.error()));
    }
    auto windowId = windowPool->tryEmplace(Html5WindowRecord{});
    if (!windowId.has_value())
    {
        return std::unexpected(std::move(windowId.error()));
    }

    // Capacity 1 for the same reason: the one canvas is the one WindowSurface.
    auto surfacePool = SurfacePool::Create(1);
    if (!surfacePool.has_value())
    {
        return std::unexpected(std::move(surfacePool.error()));
    }
    auto surfaceId = surfacePool->tryEmplace(Html5WindowSurfaceRecord{});
    if (!surfaceId.has_value())
    {
        return std::unexpected(std::move(surfaceId.error()));
    }
    auto surfaceLeaseControl = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
    if (surfaceLeaseControl == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "The HTML5 WindowSurface lease control allocation failed");
    }
    surfaceLeaseControl->ownerThread = std::this_thread::get_id();
    surfaceLeaseControl->surface = *surfaceId;

    // The CSS size is the logical extent and devicePixelRatio turns it into the
    // backing-store size, so a HiDPI display gets a sharp canvas instead of an
    // upscaled one.
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    if (emscripten_get_element_css_size(params.canvasSelector.c_str(), &cssWidth, &cssHeight) !=
        EMSCRIPTEN_RESULT_SUCCESS)
    {
        return Core::failure(PlatformErrorCode::WindowCreationFailed,
                             "The HTML5 canvas named by the selector was not found");
    }
    if (!(cssWidth > 0.0) || !(cssHeight > 0.0))
    {
        // A zero-sized canvas fails the frame builder's snapshot validation on every
        // poll, so it is rejected here where the cause is still obvious.
        return Core::failure(PlatformErrorCode::WindowCreationFailed,
                             "The HTML5 canvas has a zero CSS size");
    }

    const double devicePixelRatio = emscripten_get_device_pixel_ratio();
    const auto scale = static_cast<float>(devicePixelRatio > 0.0 ? devicePixelRatio : 1.0);
    const auto logicalWidth = static_cast<u32>(cssWidth);
    const auto logicalHeight = static_cast<u32>(cssHeight);
    const auto framebufferWidth = static_cast<u32>(cssWidth * static_cast<double>(scale));
    const auto framebufferHeight = static_cast<u32>(cssHeight * static_cast<double>(scale));

    if (params.trackElementSize)
    {
        emscripten_set_canvas_element_size(params.canvasSelector.c_str(), static_cast<int>(framebufferWidth),
                                           static_cast<int>(framebufferHeight));
    }

    WindowMetricsSnapshot metrics{};
    metrics.window = *windowId;
    metrics.logicalExtent = LogicalExtent{logicalWidth, logicalHeight};
    metrics.framebufferExtent = FramebufferExtent{framebufferWidth, framebufferHeight};
    metrics.contentScale = ContentScale{scale, scale};
    metrics.revision = 1;
    // A canvas is visible and focusable from the start. Focus is assumed rather than
    // queried because the browser reports it only through events, and a blur will
    // correct it on the first poll after it happens.
    metrics.focused = true;
    metrics.minimized = false;
    metrics.visible = true;

    auto backend = std::unique_ptr<Html5PlatformBackend>(new (std::nothrow) Html5PlatformBackend(
        std::move(*frameBuilder), std::move(*windowPool), std::move(*surfacePool), *surfaceId,
        std::move(surfaceLeaseControl), params.canvasSelector, metrics,
        params.base.frameCapacities.inputTransitionCapacity));
    if (backend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The HTML5 platform backend allocation failed");
    }

    // Registered after construction so the callbacks capture a fully built object.
    backend->registerCallbacks();

    if (params.base.primaryWindow.pointerCapture == PointerCaptureMode::Locked)
    {
        // Deferred until the first user gesture, which is the earliest a browser will
        // grant it. The request is made now so no frame is spent before it engages.
        (void)backend->setPointerCaptureMode(PointerCaptureMode::Locked);
    }

    return backend;
}

} // namespace

Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createHtml5WindowSurfacePlatformBackend(const Html5PlatformCreateParams& params)
{
    auto backend = createHtml5Backend(params);
    if (!backend.has_value())
    {
        return std::unexpected(std::move(backend.error()));
    }
    return std::unique_ptr<Integration::IWindowSurfacePlatformBackend>{std::move(*backend)};
}

Core::Result<std::unique_ptr<IPlatformBackend>> createHtml5PlatformBackend(const Html5PlatformCreateParams& params)
{
    auto backend = createHtml5Backend(params);
    if (!backend.has_value())
    {
        return std::unexpected(std::move(backend.error()));
    }
    return std::unique_ptr<IPlatformBackend>{std::move(*backend)};
}

} // namespace Tina::Platform
