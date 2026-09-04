#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/ios/IosPlatformFactory.hpp>

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#include "IosCompositionSession.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Platform {
namespace {

// Both ids are generation-checked handles, so they can only come from a pool, and the pool must
// outlive every id it hands out -- hence the backend owns both. Neither carries per-entry state: the
// adapter drives one CAMetalLayer, so capacity is 1 and the record is empty.
struct IosWindowRecord final {};
struct IosWindowSurfaceRecord final {};
using WindowPool = Core::GenerationPool<IosWindowRecord, WindowRegistryTag>;
using SurfacePool = Core::GenerationPool<IosWindowSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

// Shared by creation, by layer replacement and by a plain resize, so the three cannot drift apart --
// a rebind that accepted geometry the factory rejects would be a hole straight into bgfx::reset.
[[nodiscard]] Core::Status validateLayerGeometry(std::uintptr_t metalLayer,
                                                 FramebufferExtent framebufferExtent,
                                                 ContentScale contentScale) noexcept
{
    if (metalLayer == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The iOS platform backend requires a live CAMetalLayer handle");
    }
    // Zero extent or non-positive scale is rejected rather than defaulted: both silently mis-size
    // every UI element downstream, which is far harder to diagnose than a startup failure.
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The iOS platform backend requires a non-empty drawable extent");
    }
    if (!std::isfinite(contentScale.x) || !std::isfinite(contentScale.y) || !(contentScale.x > 0.0F) ||
        !(contentScale.y > 0.0F))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The iOS platform backend requires a finite positive content scale");
    }
    const double logicalWidth = static_cast<double>(framebufferExtent.width) / contentScale.x;
    const double logicalHeight = static_cast<double>(framebufferExtent.height) / contentScale.y;
    constexpr double MaximumLogicalExtent = static_cast<double>((std::numeric_limits<u32>::max)());
    if (!std::isfinite(logicalWidth) || !std::isfinite(logicalHeight) || logicalWidth < 1.0 ||
        logicalHeight < 1.0 || logicalWidth > MaximumLogicalExtent || logicalHeight > MaximumLogicalExtent)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The iOS drawable size and content scale produce an invalid logical extent");
    }
    return Core::success();
}

[[nodiscard]] WindowMetricsSnapshot makeWindowMetrics(WindowId window, FramebufferExtent framebufferExtent,
                                                     ContentScale contentScale, u64 revision) noexcept
{
    return WindowMetricsSnapshot{
        .window = window,
        // Logical size is derived, not queried: CAMetalLayer.drawableSize is in pixels, and the
        // native scale is the only conversion available. It is also why the host reports both --
        // bounds.size alone would already be points and could not be trusted to match the drawable
        // after a rotation.
        .logicalExtent =
            LogicalExtent{
                static_cast<u32>(static_cast<double>(framebufferExtent.width) / contentScale.x),
                static_cast<u32>(static_cast<double>(framebufferExtent.height) / contentScale.y),
            },
        .framebufferExtent = framebufferExtent,
        .contentScale = contentScale,
        .revision = revision,
        // A scene that owns a live drawable is foreground and visible. Losing either is a lifecycle
        // transition the host drives, not an initial state.
        .focused = true,
        .minimized = false,
        .visible = true,
    };
}

class IosWindowSurfacePlatformBackend final : public Integration::IWindowSurfacePlatformBackend,
                                              public IIosPlatformBackend {
  public:
    IosWindowSurfacePlatformBackend(
        PlatformFrameBuilder frameBuilder,
        WindowPool windowPool,
        SurfacePool surfacePool,
        std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl,
        std::shared_ptr<IosTouchEventQueue> touchEvents,
        std::shared_ptr<IosKeyEventQueue> keyEvents,
        std::shared_ptr<IosTextEventQueue> textEvents,
        std::shared_ptr<IosCompositionEventQueue> compositionEvents,
        Integration::WindowSurfaceId surfaceId,
        std::uintptr_t metalLayer,
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
          metalLayer_(metalLayer),
          metrics_(metrics),
          pointerState_(quiescentInput(metrics)),
          surfaceSnapshot_(makeSurfaceSnapshot(surfaceId, metrics, 1)),
          ownerThread_(std::this_thread::get_id())
    {
        leaseControl_->surface = surfaceId_;
        leaseControl_->binding = Integration::Detail::NativeWindowBinding{
            .kind = Integration::Detail::NativeWindowBindingKind::Ios,
            .nativeDisplay = 0,
            .nativeWindow = metalLayer_,
            .bindingRevision = surfaceSnapshot_.nativeBindingRevision,
        };
    }

    ~IosWindowSurfacePlatformBackend() noexcept override
    {
        shutdown();
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
                                 "The iOS platform frame sequence is exhausted");
        }

        auto beginStatus = frameBuilder_.beginFrame(PlatformFrameId{nextFrameId_++});
        if (!beginStatus)
        {
            return std::unexpected(std::move(beginStatus.error()));
        }
        auto discardPartialFrame = Core::makeScopeExit([this]() noexcept {
            (void)frameBuilder_.discardFrame();
            streamRecoveryPending_ = true;
        });

        resetPointerDeltas();
        if (inputQueueDropObserved())
        {
            streamRecoveryPending_ = true;
        }

        // Transitions first, then the end-of-poll snapshot. Order matters: a transition carries the
        // position captured at that exact moment, while the snapshot is the state the poll ended in,
        // and downstream relies on both being consistent with each other.
        if (streamRecoveryPending_)
        {
            resetInputStreamState();
            if (auto status = append(InputStreamReset{
                    .routedWindow = metrics_.window,
                    .reason = InputResetReason::BackendRecovery,
                });
                !status)
            {
                return std::unexpected(std::move(status.error()));
            }
            streamRecoveryPending_ = false;
        }
        else
        {
            if (windowCancelPending_)
            {
                if (auto status = append(InputCancelTransition{
                        .routedWindow = metrics_.window,
                        .reason = InputCancelReason::FocusLost,
                    });
                    !status)
                {
                    return std::unexpected(std::move(status.error()));
                }
                windowCancelPending_ = false;
            }

            if (surfaceSnapshot_.suspended)
            {
                discardQueuedInput();
                if (auto status = drainCompositionEvents(); !status)
                {
                    return std::unexpected(std::move(status.error()));
                }
            }
            else
            {
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
            }
        }
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics_, pointerState_))
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "The iOS platform backend could not publish its window snapshot");
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
        discardPartialFrame.release();
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
            caretPoints_.reset();
            return Core::success();
        }
        // Latched rather than rejected. Rejecting a non-null placement would make the engine unusable
        // with a focused TextEdit: the Runtime publishes a caret every frame one is focused, the
        // coordinator turns this failure into a LifecycleInvariantViolation, and EngineHost latches a
        // terminal outcome -- the app simply stops producing frames, with the caret as the only cause.
        //
        // The host reads it and returns it from firstRectForRange:/caretRectForPosition:. Unlike
        // IMM32 there is nothing to position from C++: the candidate bar and the text magnifier both
        // belong to UIKit, which asks for geometry rather than being told.
        if (placement->window != metrics_.window)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The iOS text input placement belongs to another window");
        }
        const TextInputCaretRect& caret = placement->caret;
        // Validated rather than clamped: a non-finite or non-positive-height caret means the UI
        // published geometry it should not have, and silently substituting a plausible rectangle
        // would place the candidate bar somewhere arbitrary with nothing to trace it back to.
        if (!isFinitePositiveCaret(caret))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The iOS text input caret geometry must be finite with a positive height");
        }
        // Window-logical units and UIKit points are the same space here: logical extent is the
        // drawable divided by the native scale, which is exactly how UIKit derives bounds from the
        // backing store. So this is a narrowing conversion, not a unit conversion -- and it is
        // range-checked because a double that does not fit a float would otherwise become infinity.
        auto points = toCaretPoints(caret);
        if (!points)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The iOS text input caret exceeds the representable point range");
        }
        caretPoints_ = *points;
        return Core::success();
    }

    Core::Status setPointerCaptureMode(PointerCaptureMode mode) override
    {
        if (auto status = checkUsable("pointer capture mode"); !status)
        {
            return status;
        }
        // iOS touch is a sequence of discrete contact events with no persisted cursor entity, so
        // locking a cursor is a category error: there is nothing to lock. The Free request matches
        // what touch always is.
        if (mode != PointerCaptureMode::Free)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "iOS touch input has no cursor to lock");
        }
        return Core::success();
    }

    void shutdown() noexcept override
    {
        if (stopped_)
        {
            return;
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (leaseControl_ != nullptr && leaseControl_->activeLeaseCount != 0)
        {
            std::terminate();
        }
        stopped_ = true;
        if (leaseControl_ != nullptr)
        {
            leaseControl_->surfaceAlive = false;
        }
        if (metrics_.window.hasValue())
        {
            (void)windowPool_.erase(metrics_.window);
            metrics_.window = {};
        }
        if (surfaceId_.hasValue())
        {
            (void)surfacePool_.erase(surfaceId_);
            surfaceId_ = {};
        }
        metalLayer_ = 0;
    }

    [[nodiscard]] Core::Status publishPrimaryWindow() noexcept override
    {
        if (auto status = checkUsable("publish primary window"); !status)
        {
            return status;
        }
        // UIKit has already put the view on screen by the time the host owns a CAMetalLayer; there is
        // no hidden-window phase to end, so publication is a state assertion rather than an action.
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
        if (leaseControl_ != nullptr && leaseControl_->activeLeaseCount != 0)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired,
                                 "The iOS primary WindowSurface lease was already acquired");
        }

        const Integration::Detail::NativeWindowBinding binding{
            .kind = Integration::Detail::NativeWindowBindingKind::Ios,
            // A CAMetalLayer is self-contained; the bgfx decoder rejects a display here.
            .nativeDisplay = 0,
            .nativeWindow = metalLayer_,
            .bindingRevision = surfaceSnapshot_.nativeBindingRevision,
        };
        auto lease =
            Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(leaseControl_, surfaceId_, binding);
        if (!lease)
        {
            return std::unexpected(std::move(lease.error()));
        }
        return std::move(*lease);
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    primaryWindowSurfaceSnapshot() const noexcept override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The iOS primary WindowSurface snapshot is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The iOS WindowSurface snapshot must be read on the owner thread");
        }
        return surfaceSnapshot_;
    }

    // --- IIosPlatformBackend ---

    Core::Status onNativeLayerReleased() noexcept override
    {
        if (auto status = checkUsable("native layer released"); !status)
        {
            return status;
        }
        if (metalLayer_ == 0)
        {
            // Idempotent: UIKit can tear a scene down without a preceding acquisition, and failing
            // there would turn a normal lifecycle into an error.
            return Core::success();
        }
        if (metrics_.revision == (std::numeric_limits<u64>::max)() ||
            (!surfaceRevisionPendingObservation_ &&
             surfaceSnapshot_.surfaceRevision == (std::numeric_limits<u64>::max)()))
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The iOS WindowSurface revision is exhausted");
        }

        // Suspended, not "resized to nothing": the drawable is genuinely gone, and ADR 0034 is
        // explicit that surfaceSuspended does not mean device lost -- the Metal device survives, only
        // the drawable does not.
        if (auto status = markSurfaceFactsChanged(); !status)
        {
            return status;
        }
        metalLayer_ = 0;
        ++metrics_.revision;
        metrics_.focused = false;
        metrics_.visible = false;
        metrics_.minimized = true;
        pointerState_.sourceMetricsRevision = metrics_.revision;
        surfaceSnapshot_.suspended = true;
        surfaceSnapshot_.sourceMetricsRevision = metrics_.revision;

        // Every finger is gone with the drawable. Leaving slots mapped is precisely the cocos2d-x
        // failure where a drag interrupted by a task switch stranded its finger until process exit --
        // and it is worse here than on Android, because UIKit delivers no touchesCancelled: at all
        // when a scene is disconnected.
        releaseAllPointers();
        windowCancelPending_ = true;
        discardQueuedInput();

        // A composition in flight goes with the drawable too. Without this the UI keeps drawing a
        // preedit the input system has already forgotten, and the next marked pass would report
        // Updated for a session the consumer never saw start. Queued into the composition ring rather
        // than appended here: this runs outside a frame, and appending to a builder that has not
        // begun a frame is a failure.
        if (composition_.active())
        {
            compositionCancelPending_ = true;
        }

        // The caret described a layer that no longer exists. Keeping it would have the host return a
        // stale rectangle for the next drawable, whose geometry may differ entirely.
        caretPoints_.reset();
        return Core::success();
    }

    Core::Status onNativeLayerAcquired(IosNativeLayerHandle layer, FramebufferExtent framebufferExtent,
                                      ContentScale contentScale) noexcept override
    {
        if (auto status = checkUsable("native layer acquired"); !status)
        {
            return status;
        }
        if (auto status = validateLayerGeometry(layer.metalLayer, framebufferExtent, contentScale); !status)
        {
            return status;
        }

        const bool bindingChanged = metalLayer_ == 0 || metalLayer_ != layer.metalLayer;
        const bool geometryChanged = metrics_.framebufferExtent != framebufferExtent ||
                                     metrics_.contentScale != contentScale || surfaceSnapshot_.suspended ||
                                     !metrics_.focused || !metrics_.visible || metrics_.minimized;
        if (!bindingChanged && !geometryChanged)
        {
            // layoutSubviews fires without changing either the layer or its drawable size. Treating
            // that as a rebind would churn swapchains and revisions on every layout pass, which on
            // iOS happens far more often than the equivalent Android callback.
            return Core::success();
        }

        if (metrics_.revision == (std::numeric_limits<u64>::max)() ||
            (bindingChanged &&
             surfaceSnapshot_.nativeBindingRevision == (std::numeric_limits<u64>::max)()) ||
            (!surfaceRevisionPendingObservation_ &&
             surfaceSnapshot_.surfaceRevision == (std::numeric_limits<u64>::max)()))
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The iOS native layer revision is exhausted");
        }

        u64 nextBindingRevision = surfaceSnapshot_.nativeBindingRevision;
        if (bindingChanged)
        {
            ++nextBindingRevision;
            const Integration::Detail::NativeWindowBinding binding{
                .kind = Integration::Detail::NativeWindowBindingKind::Ios,
                .nativeDisplay = 0,
                .nativeWindow = layer.metalLayer,
                .bindingRevision = nextBindingRevision,
            };
            if (auto status = Integration::Detail::NativeWindowSurfaceLeaseAccess::rebind(
                    leaseControl_, surfaceId_, binding);
                !status)
            {
                return status;
            }
        }

        if (auto status = markSurfaceFactsChanged(); !status)
        {
            return status;
        }
        metalLayer_ = layer.metalLayer;
        metrics_ = makeWindowMetrics(metrics_.window, framebufferExtent, contentScale, metrics_.revision + 1);
        // pointerState_ must be rebuilt, not carried: it holds the previous metrics revision, and a
        // published frame is rejected unless the two agree.
        pointerState_ = quiescentInput(metrics_);
        discardQueuedInput();
        windowCancelPending_ = true;
        // The tracker requires all three to move together, so a consumer can never observe the rebind
        // and the geometry it must reset to in two different frames.
        surfaceSnapshot_.nativeBindingRevision = nextBindingRevision;
        surfaceSnapshot_.framebufferExtent = framebufferExtent;
        surfaceSnapshot_.contentScale = contentScale;
        surfaceSnapshot_.sourceMetricsRevision = metrics_.revision;
        surfaceSnapshot_.suspended = false;

        // The keyboard's occlusion was measured against the previous drawable. Rotating from portrait
        // to landscape changes both the window height and the keyboard height, so carrying the old
        // value over would report an occlusion that never matched either geometry.
        softKeyboardOccludedPhysicalHeight_ = 0;
        return Core::success();
    }

    Core::Status onDrawableResized(FramebufferExtent framebufferExtent,
                                  ContentScale contentScale) noexcept override
    {
        if (auto status = checkUsable("drawable resized"); !status)
        {
            return status;
        }
        if (metalLayer_ == 0)
        {
            // A resize with no drawable is a rotation while the scene is disconnected. The next
            // acquisition carries the final geometry, so there is nothing to publish here -- and
            // advancing revisions for it would make the surface look alive while it is suspended.
            return Core::success();
        }
        if (auto status = validateLayerGeometry(metalLayer_, framebufferExtent, contentScale); !status)
        {
            return status;
        }
        if (metrics_.framebufferExtent == framebufferExtent && metrics_.contentScale == contentScale)
        {
            return Core::success();
        }
        if (metrics_.revision == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                 "The iOS window metrics revision is exhausted");
        }

        if (auto status = markSurfaceFactsChanged(); !status)
        {
            return status;
        }
        // nativeBindingRevision deliberately untouched: the CAMetalLayer is the same object, so the
        // render device needs a reset against the new drawable size, not a rebind. Advancing it here
        // would have the device throw away a swapchain that is still valid, which on Metal means a
        // visible hitch on every rotation.
        metrics_ = makeWindowMetrics(metrics_.window, framebufferExtent, contentScale, metrics_.revision + 1);
        // Rebuilt for the same reason as in onNativeLayerAcquired: the carried snapshot holds the old
        // metrics revision, and the frame is rejected unless the two agree. Fingers are released with
        // it -- a rotation reflows the whole UI, so a finger's logical position no longer refers to
        // anything it was touching.
        pointerState_ = quiescentInput(metrics_);
        windowCancelPending_ = true;
        surfaceSnapshot_.framebufferExtent = framebufferExtent;
        surfaceSnapshot_.contentScale = contentScale;
        surfaceSnapshot_.sourceMetricsRevision = metrics_.revision;

        // Same reasoning as the acquisition path: the previous occlusion was measured against a
        // window height that no longer exists.
        softKeyboardOccludedPhysicalHeight_ = 0;
        // The caret is in points against the old layout. The UI republishes it on the next frame it
        // has focus, so dropping it costs one frame of candidate-bar placement and avoids reporting a
        // rectangle that points at pre-rotation geometry.
        caretPoints_.reset();
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
        return setSoftKeyboardRequest(IosSoftKeyboardRequest::Show);
    }

    [[nodiscard]] Core::Status requestHideSoftKeyboard() noexcept override
    {
        return setSoftKeyboardRequest(IosSoftKeyboardRequest::Hide);
    }

    [[nodiscard]] Core::Status onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept override
    {
        if (auto status = checkUsable("soft keyboard occlusion"); !status)
        {
            return status;
        }
        // A keyboard taller than the window means the host measured against different geometry than
        // the one this backend holds -- most likely it reported before forwarding a rotation.
        // Clamping would hide that disagreement.
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

    [[nodiscard]] IosSoftKeyboardRequest pendingSoftKeyboardRequest() const noexcept override
    {
        return pendingSoftKeyboardRequest_;
    }

    [[nodiscard]] Core::Status
    acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest request) noexcept override
    {
        if (auto status = checkUsable("soft keyboard request acknowledgement"); !status)
        {
            return status;
        }
        if (request == IosSoftKeyboardRequest::None)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "A soft keyboard acknowledgement must name a pending request");
        }
        if (pendingSoftKeyboardRequest_ == request)
        {
            pendingSoftKeyboardRequest_ = IosSoftKeyboardRequest::None;
        }
        return Core::success();
    }

    [[nodiscard]] std::optional<IosCaretPoints> caretPoints() const noexcept override
    {
        // Deliberately non-consuming, unlike the keyboard request: UIKit asks for caret geometry
        // whenever it repositions the candidate bar or the magnifier, so clearing on read would leave
        // the second query with nothing.
        return caretPoints_;
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

    // Window-logical -> UIKit points, which are the same unit but a narrower type.
    //
    // Range-checked rather than cast: a double outside float's range becomes infinity, and UIKit
    // given an infinite CGRect places the candidate bar at an undefined position instead of failing.
    [[nodiscard]] static std::optional<IosCaretPoints> toCaretPoints(const TextInputCaretRect& caret) noexcept
    {
        const auto convert = [](double value) noexcept -> std::optional<float> {
            constexpr double MaximumFloat = static_cast<double>((std::numeric_limits<float>::max)());
            if (value < -MaximumFloat || value > MaximumFloat)
            {
                return std::nullopt;
            }
            return static_cast<float>(value);
        };
        const auto x = convert(caret.x);
        const auto y = convert(caret.y);
        const auto width = convert(caret.width);
        const auto height = convert(caret.height);
        if (!x || !y || !width || !height)
        {
            return std::nullopt;
        }
        return IosCaretPoints{.x = *x, .y = *y, .width = *width, .height = *height};
    }

    [[nodiscard]] Core::Status setSoftKeyboardRequest(IosSoftKeyboardRequest request) noexcept
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
        // Keys go with the drawable for the same reason fingers do: a key held when the scene is
        // disconnected will never deliver an Up, so leaving the bit set latches it for the rest of
        // the run.
        pointerState_.heldKeys.reset();
    }

    void resetPointerDeltas() noexcept
    {
        for (auto& pointer : pointerState_.pointers)
        {
            pointer.accumulatedDeltaX = 0.0;
            pointer.accumulatedDeltaY = 0.0;
        }
    }

    void discardQueuedInput() noexcept
    {
        IosTouchEvent touch{};
        while (touchEvents_ != nullptr && touchEvents_->tryPop(touch))
        {
        }
        IosKeyEvent key{};
        while (keyEvents_ != nullptr && keyEvents_->tryPop(key))
        {
        }
        IosTextEvent text{};
        while (textEvents_ != nullptr && textEvents_->tryPop(text))
        {
        }
        IosCompositionEvent composition{};
        while (compositionEvents_ != nullptr && compositionEvents_->tryPop(composition))
        {
        }
    }

    void resetInputStreamState() noexcept
    {
        releaseAllPointers();
        discardQueuedInput();
        composition_ = {};
        compositionCancelPending_ = false;
        windowCancelPending_ = false;
    }

    template <typename Queue>
    [[nodiscard]] static bool observeQueueDrops(const std::shared_ptr<Queue>& queue, u64& observed) noexcept
    {
        const u64 current = queue == nullptr ? 0 : queue->droppedEventCount();
        const bool changed = current != observed;
        observed = current;
        return changed;
    }

    [[nodiscard]] bool inputQueueDropObserved() noexcept
    {
        const bool touchDropped = observeQueueDrops(touchEvents_, observedDroppedTouchEvents_);
        const bool keyDropped = observeQueueDrops(keyEvents_, observedDroppedKeyEvents_);
        const bool textDropped = observeQueueDrops(textEvents_, observedDroppedTextEvents_);
        const bool compositionDropped =
            observeQueueDrops(compositionEvents_, observedDroppedCompositionEvents_);
        return touchDropped || keyDropped || textDropped || compositionDropped;
    }

    // Records that the surface facts changed, advancing surfaceRevision at most once per observation.
    //
    // Incrementing on every lifecycle call is wrong, and cost a real debugging round on Android:
    // release and acquire arrive back to back during a background/foreground cycle with no poll in
    // between, so two eager increments made the revision jump by 2 and RenderSurfaceStateTracker
    // rejected every subsequent frame with "Render surface revision must advance exactly once for
    // each committed state change" -- the screen simply went black with no other symptom. iOS is more
    // exposed to it, not less: a scene reconnection can pair a release, an acquire and a resize before
    // the next CADisplayLink callback runs.
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
                                 "The iOS WindowSurface revision is exhausted");
        }
        ++surfaceSnapshot_.surfaceRevision;
        surfaceRevisionPendingObservation_ = true;
        return Core::success();
    }

    // The starting input state, carried across polls and mutated by each drained event.
    //
    // Not a default-constructed WindowInputSnapshot: that one fails PlatformFrameBuilder's
    // window-snapshot validation outright, because the input's window id and metrics revision must
    // match the metrics it accompanies.
    //
    // Nor does it leave pointers `present`. That default suits a mouse, which always has a position,
    // and is wrong for touch: between taps a finger has no position at all, so a present-but-idle
    // pointer would latch hover on whatever was last touched (ADR 0032 C2). Absent is the honest
    // steady state, and a Began is what makes a pointer present.
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

    // UIKit points -> window-logical. The two are the same unit by construction (logical extent is
    // the drawable divided by the same native scale UIKit uses), so this is identity today. It is
    // written as a conversion anyway because the alternative is scattering the assumption across
    // every touch event, where a future divergence would show up as a silently mis-scaled gesture.
    [[nodiscard]] static double toLogical(float points) noexcept
    {
        return static_cast<double>(points);
    }

    // Drains every queued touch into this frame's transitions, updating the carried pointer state as
    // it goes.
    [[nodiscard]] Core::Status drainTouchEvents() noexcept
    {
        if (touchEvents_ == nullptr)
        {
            return Core::success();
        }

        IosTouchEvent event{};
        while (touchEvents_->tryPop(event))
        {
            if (event.pointerSlot >= PointerCapacity)
            {
                // The producer already reports a full slot table by dropping the event, so a bad slot
                // here means a producer bug rather than a busy device. Rejecting keeps it from
                // corrupting an unrelated pointer's state.
                return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                     "An iOS touch event carried an out-of-range pointer slot");
            }

            const auto slot = static_cast<PointerId>(event.pointerSlot);
            PointerSnapshot& pointer = pointerState_.pointers[event.pointerSlot];
            const double logicalX = toLogical(event.pointX);
            const double logicalY = toLogical(event.pointY);

            switch (event.phase)
            {
            case IosTouchPhase::Began:
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

            case IosTouchPhase::Moved:
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

            case IosTouchPhase::Ended:
                // Position is kept, not zeroed: the lift's own coordinates are what a tap is judged
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

            case IosTouchPhase::Cancelled:
                // Scoped to this one pointer, never the whole window. A window-wide cancel is exactly
                // the defect ADR 0032 cites from cocos2d-x: one finger leaving would make the others
                // drop the controls they are still holding. `pointer` being set is what makes this
                // per-pointer; nullopt would mean all eight.
                pointer.heldButtons.reset();
                pointer.present = false;
                if (auto status = append(InputCancelTransition{
                        .routedWindow = metrics_.window,
                        // FocusLost is the closest existing reason: UIKit takes the gesture away when
                        // a system interaction claims it. No new enumerator is invented here -- that
                        // would be a public contract change for no behavioural difference.
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

        IosKeyEvent event{};
        while (keyEvents_->tryPop(event))
        {
            const Key key = iosKeyFromHidUsage(event.hidUsage);
            if (key == Key::Unknown)
            {
                // Dropped rather than reported as Unknown: publishing it would let a consumer key off
                // an enumerator that means "we could not identify this", which is never actionable.
                continue;
            }

            const auto keyIndex = static_cast<usize>(key);
            const bool down = event.action == IosKeyAction::Down;
            // A repeat must not re-set an already-held bit as if it were a new press, and an Up for a
            // key we never saw down must not clear a bit that was never set. Both are routine on iOS,
            // where UIKit stops delivering key events the moment a view resigns first responder --
            // so the Up for a held key can simply never arrive.
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

    // Drains committed text into this frame's transitions.
    //
    // Unlike keys, text carries no held state: a commit is a one-shot event, so there is nothing in
    // WindowInputSnapshot to update. That also means a dropped commit is simply lost input rather
    // than a stuck state, which is why the queue may be lossy at all.
    [[nodiscard]] Core::Status drainTextEvents() noexcept
    {
        if (textEvents_ == nullptr)
        {
            return Core::success();
        }

        IosTextEvent event{};
        while (textEvents_->tryPop(event))
        {
            // `event` outlives the append call, which matters: TextInputTransition holds a borrowed
            // view, and PlatformFrameBuilder copies it into its own arena during append. Passing a
            // view into a temporary here would dangle before the copy happened.
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

    // Drains marked-text events, publishing each one's stage and any text it resolved into.
    //
    // Commits travel through this queue too, which is the point: a commit that ends a marked pass
    // must publish Ended *before* its text, and the session is the only thing that knows whether a
    // composition was in flight. Two queues could not express that relationship at all -- they would
    // be drained one after the other, so every commit would land before every stage of the pass that
    // produced it.
    [[nodiscard]] Core::Status drainCompositionEvents() noexcept
    {
        // The layer-loss cancel is published here rather than where it happens, because that path
        // runs outside a frame and PlatformFrameBuilder rejects an append with no frame begun.
        // Latching it also collapses repeated lifecycle calls into the one cancel the session
        // actually has to make.
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

        IosCompositionEvent event{};
        while (compositionEvents_->tryPop(event))
        {
            const Detail::IosCompositionOutcome outcome = composition_.apply(event);
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
    [[nodiscard]] Core::Status publishComposition(const Detail::IosCompositionOutcome& outcome) noexcept
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

    // Capacity exhaustion is a real failure, not something to swallow: a dropped transition means a
    // press with no matching release downstream.
    [[nodiscard]] Core::Status append(InputTransitionPayload payload) noexcept
    {
        const FrameBatchAppendResult result = frameBuilder_.appendInputTransition(std::move(payload));
        switch (result)
        {
        case FrameBatchAppendResult::Appended:
        case FrameBatchAppendResult::Coalesced:
        case FrameBatchAppendResult::ResetInserted:
        case FrameBatchAppendResult::IgnoredAfterReset:
            return Core::success();
        case FrameBatchAppendResult::RejectedCapacity:
            return Core::failure(PlatformErrorCode::InvalidFrameCapacity,
                                 "The iOS touch batch exceeded the frame input transition capacity");
        default:
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "The iOS platform backend produced an invalid input transition");
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
            return Core::failure(PlatformErrorCode::BackendStopped, "The iOS platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The iOS platform backend must be used on its owner thread");
        }
        (void)operation;
        return Core::success();
    }

    PlatformFrameBuilder frameBuilder_;
    WindowPool windowPool_;
    SurfacePool surfacePool_;
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl_;
    std::shared_ptr<IosTouchEventQueue> touchEvents_;
    std::shared_ptr<IosKeyEventQueue> keyEvents_;
    std::shared_ptr<IosTextEventQueue> textEvents_;
    std::shared_ptr<IosCompositionEventQueue> compositionEvents_;
    // Owner-thread only, like every other piece of drained state: it is mutated exclusively while
    // draining, which happens inside pollFrame.
    Detail::IosCompositionSession composition_{};
    u64 publishedTextCommits_ = 0;
    u64 compositionStarts_ = 0;
    u64 compositionUpdates_ = 0;
    u64 compositionEnds_ = 0;
    u64 compositionCancels_ = 0;
    u64 observedDroppedTouchEvents_ = 0;
    u64 observedDroppedKeyEvents_ = 0;
    u64 observedDroppedTextEvents_ = 0;
    u64 observedDroppedCompositionEvents_ = 0;
    // Set when the drawable went away with a composition in flight, cleared by the poll that
    // publishes the cancel. See drainCompositionEvents for why it cannot be published at the point it
    // happens.
    bool compositionCancelPending_ = false;
    bool streamRecoveryPending_ = false;
    bool windowCancelPending_ = false;
    std::optional<IosCaretPoints> caretPoints_{};
    u32 softKeyboardOccludedPhysicalHeight_ = 0;
    IosSoftKeyboardRequest pendingSoftKeyboardRequest_ = IosSoftKeyboardRequest::None;
    Integration::WindowSurfaceId surfaceId_{};
    std::uintptr_t metalLayer_ = 0;
    WindowMetricsSnapshot metrics_{};
    // Carried across polls: a finger stays down between frames, so the snapshot is state rather than
    // something rebuilt per poll.
    WindowInputSnapshot pointerState_{};
    Integration::WindowSurfaceSnapshot surfaceSnapshot_{};
    // True between a surface-facts change and the poll that publishes it, so back-to-back lifecycle
    // events collapse into one revision step. See markSurfaceFactsChanged for why that matters.
    bool surfaceRevisionPendingObservation_ = false;
    std::thread::id ownerThread_{};
    u64 nextFrameId_ = 1;
    bool stopped_ = false;
    bool published_ = false;
};

} // namespace

Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createIosWindowSurfacePlatformBackend(const IosPlatformBackendCreateParams& params)
{
    if (auto status =
            validateLayerGeometry(params.layer.metalLayer, params.framebufferExtent, params.contentScale);
        !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    auto frameBuilder = PlatformFrameBuilder::Create(params.platform.frameCapacities);
    if (!frameBuilder)
    {
        return std::unexpected(std::move(frameBuilder.error()));
    }

    // Both pools are created before the metrics they identify, and both must outlive their ids, so
    // ownership moves into the backend below. Capacity 1: one scene, one drawable layer.
    auto windowPool = WindowPool::Create(1);
    if (!windowPool)
    {
        return std::unexpected(std::move(windowPool.error()));
    }
    auto windowId = windowPool->tryEmplace(IosWindowRecord{});
    if (!windowId)
    {
        return std::unexpected(std::move(windowId.error()));
    }
    auto surfacePool = SurfacePool::Create(1);
    if (!surfacePool)
    {
        return std::unexpected(std::move(surfacePool.error()));
    }
    auto surfaceId = surfacePool->tryEmplace(IosWindowSurfaceRecord{});
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
            std::make_unique<IosWindowSurfacePlatformBackend>(
                std::move(*frameBuilder), std::move(*windowPool), std::move(*surfacePool),
                std::move(leaseControl), params.touchEvents, params.keyEvents, params.textEvents,
                params.compositionEvents, *surfaceId, params.layer.metalLayer, metrics)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The iOS platform backend allocation failed");
    }
}

} // namespace Tina::Platform
