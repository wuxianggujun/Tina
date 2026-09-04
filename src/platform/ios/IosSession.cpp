#include <tina/platform/ios/IosSession.hpp>

#include <array>
#include <new>
#include <span>
#include <utility>

namespace Tina::Platform {
namespace {

[[nodiscard]] IosTouchEvent makeTouch(IosTouchPhase phase, u8 slot, float pointX, float pointY) noexcept
{
    return IosTouchEvent{.phase = phase, .pointerSlot = slot, .pointX = pointX, .pointY = pointY};
}

} // namespace

Core::Result<std::unique_ptr<IosSession>> IosSession::Create()
{
    try
    {
        return std::unique_ptr<IosSession>{new IosSession{}};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The iOS session allocation failed");
    }
}

IosSession::~IosSession() noexcept
{
    shutdown();
}

Core::Status IosSession::bindLayer(IosNativeLayerHandle layer, FramebufferExtent framebufferExtent,
                                   ContentScale contentScale) noexcept
{
    if (stopped_)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The iOS session has been shut down");
    }
    if (iosBackend_ != nullptr)
    {
        return iosBackend_->onNativeLayerAcquired(layer, framebufferExtent, contentScale);
    }

    IosPlatformBackendCreateParams params{};
    params.layer = layer;
    params.framebufferExtent = framebufferExtent;
    params.contentScale = contentScale;
    params.touchEvents = touchEvents_;
    params.keyEvents = keyEvents_;
    params.textEvents = textEvents_;
    params.compositionEvents = compositionEvents_;

    auto created = createIosWindowSurfacePlatformBackend(params);
    if (!created)
    {
        return Core::failure(std::move(created.error()));
    }
    auto* facet = dynamic_cast<IIosPlatformBackend*>(created->get());
    if (facet == nullptr)
    {
        (*created)->shutdown();
        return Core::failure(Core::CoreErrorCode::Internal,
                             "The iOS platform backend does not expose IIosPlatformBackend");
    }
    backend_ = std::move(*created);
    iosBackend_ = facet;
    return Core::success();
}

Core::Status IosSession::resizeDrawable(FramebufferExtent framebufferExtent,
                                        ContentScale contentScale) noexcept
{
    if (stopped_)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The iOS session has been shut down");
    }
    if (iosBackend_ == nullptr)
    {
        // No drawable yet: the next bind carries the final size. Matching onDrawableResized,
        // which is a silent success when the layer is already gone.
        return Core::success();
    }
    return iosBackend_->onDrawableResized(framebufferExtent, contentScale);
}

void IosSession::unbindLayer() noexcept
{
    if (iosBackend_ == nullptr)
    {
        return;
    }
    (void)iosBackend_->onNativeLayerReleased();
    // The backend clears its own pointer snapshot; the slot table is session-owned and would
    // otherwise keep mapping UITouch addresses that UIKit is about to reuse.
    slots_.releaseAll();
}

bool IosSession::onTouch(std::uintptr_t touchIdentity, IosTouchPhase phase, float pointX,
                         float pointY) noexcept
{
    if (stopped_ || touchIdentity == 0)
    {
        // 0 is IosTouchSlotTable's unused sentinel. A real UITouch is never null, so treating 0
        // as a finger would claim every unused slot is tracking one.
        return false;
    }

    u8 slot = IosTouchSlotTable::InvalidSlot;
    switch (phase)
    {
    case IosTouchPhase::Began:
        slot = slots_.acquire(touchIdentity);
        break;
    case IosTouchPhase::Moved:
        slot = slots_.find(touchIdentity);
        break;
    case IosTouchPhase::Ended:
    case IosTouchPhase::Cancelled:
        slot = slots_.find(touchIdentity);
        slots_.release(touchIdentity);
        break;
    }
    if (slot == IosTouchSlotTable::InvalidSlot)
    {
        return false;
    }
    if (!touchEvents_->tryPush(makeTouch(phase, slot, pointX, pointY)))
    {
        // The lost event may have been the only Ended/Cancelled. Clearing the mapping forces
        // subsequent Moves to be rejected until a fresh Began, and the backend observes the
        // drop counter and publishes a window-wide cancel.
        slots_.releaseAll();
        return false;
    }
    return true;
}

bool IosSession::onKey(i32 hidUsage, IosKeyAction action, bool repeat) noexcept
{
    if (stopped_)
    {
        return false;
    }
    if (iosKeyFromHidUsage(hidUsage) == Key::Unknown)
    {
        return false;
    }
    return keyEvents_->tryPush(IosKeyEvent{.action = action, .hidUsage = hidUsage, .repeat = repeat});
}

bool IosSession::onTextCommitUtf16(std::u16string_view utf16) noexcept
{
    if (stopped_)
    {
        return false;
    }
    std::array<IosCompositionEvent, IosCompositionEventCapacity> events{};
    usize eventCount = 0;
    if (!makeIosCommitEventsFromUtf16(utf16, std::span<IosCompositionEvent>{events}, eventCount) ||
        eventCount == 0)
    {
        return false;
    }
    return compositionEvents_->tryPushBatch(
        std::span<const IosCompositionEvent>{events}.first(eventCount));
}

bool IosSession::onSetMarkedTextUtf16(std::u16string_view utf16, i32 cursorUtf16Offset) noexcept
{
    if (stopped_)
    {
        return false;
    }
    IosCompositionEvent event{};
    if (!makeIosCompositionEventFromUtf16(utf16, cursorUtf16Offset, IosCompositionAction::SetMarkedText,
                                          event))
    {
        return false;
    }
    return compositionEvents_->tryPush(event);
}

bool IosSession::onUnmarkText() noexcept
{
    if (stopped_)
    {
        return false;
    }
    IosCompositionEvent event{};
    event.action = IosCompositionAction::Unmark;
    return compositionEvents_->tryPush(event);
}

IosSoftKeyboardRequest IosSession::pendingSoftKeyboardRequest() const noexcept
{
    if (iosBackend_ == nullptr)
    {
        return IosSoftKeyboardRequest::None;
    }
    return iosBackend_->pendingSoftKeyboardRequest();
}

Core::Status IosSession::acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest request) noexcept
{
    if (iosBackend_ == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The iOS session has no bound layer");
    }
    return iosBackend_->acknowledgeSoftKeyboardRequest(request);
}

Core::Status IosSession::onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept
{
    if (iosBackend_ == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The iOS session has no bound layer");
    }
    return iosBackend_->onSoftKeyboardOcclusionChanged(occludedPhysicalHeight);
}

std::optional<IosCaretPoints> IosSession::caretPoints() const noexcept
{
    if (iosBackend_ == nullptr)
    {
        return std::nullopt;
    }
    return iosBackend_->caretPoints();
}

Core::Result<PlatformPollResult> IosSession::pollFrame()
{
    if (stopped_ || backend_ == nullptr)
    {
        return PlatformPollResult::Exit();
    }
    return backend_->pollFrame();
}

IIosPlatformBackend* IosSession::facet() noexcept
{
    return iosBackend_;
}

Integration::IWindowSurfacePlatformBackend* IosSession::backend() noexcept
{
    return backend_.get();
}

void IosSession::shutdown() noexcept
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    iosBackend_ = nullptr;
    if (backend_ != nullptr)
    {
        backend_->shutdown();
        backend_.reset();
    }
    slots_.releaseAll();
}

} // namespace Tina::Platform
