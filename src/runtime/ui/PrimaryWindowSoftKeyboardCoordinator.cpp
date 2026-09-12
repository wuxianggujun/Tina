#include "PrimaryWindowSoftKeyboardCoordinator.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITextSystem.hpp>

#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Status lifecycleFailure(std::string_view message,
                                            std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext("PrimaryWindowSoftKeyboardCoordinator::publish", detail);
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowSoftKeyboardCoordinator::PrimaryWindowSoftKeyboardCoordinator() noexcept
    : ownerThreadId_(std::this_thread::get_id())
{
}

Core::Status PrimaryWindowSoftKeyboardCoordinator::publish(
    UI::UIContext* context, Platform::IPlatformBackend& backend)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "Soft keyboard requests must be published on its owner thread");
    }

    Platform::ISoftKeyboard* softKeyboard = backend.softKeyboard();
    if (softKeyboard == nullptr)
    {
        // Backend has no soft keyboard capability (desktop, headless, web).
        // This is expected and not an error. Reset tracking state so we don't
        // send stale requests if the backend is later swapped to one that has
        // soft keyboard support (not currently possible, but keeps the contract
        // clean).
        previousTextInputFocus_ = UI::UINodeId{};
        return Core::success();
    }

    const UI::UINodeId currentTextInputFocus = context != nullptr
                                                  ? context->text().imeFocus()
                                                  : UI::UINodeId{};

    // Focus change detection: compare current committed focus against the
    // previous frame's committed focus. This avoids sending duplicate requests
    // on every frame while focus is stable.
    const bool previousHadFocus = previousTextInputFocus_.hasValue();
    const bool currentHasFocus = currentTextInputFocus.hasValue();

    if (!previousHadFocus && currentHasFocus)
    {
        // Focus acquired: None → Valid TextEdit node.
        softKeyboard->requestShow();
    }
    else if (previousHadFocus && !currentHasFocus)
    {
        // Focus released: Valid TextEdit node → None.
        softKeyboard->requestHide();
    }
    // Focus unchanged (None → None or Valid → Valid) or focus moved between
    // two different TextEdit nodes (Valid → Valid): no keyboard request.

    previousTextInputFocus_ = currentTextInputFocus;
    return Core::success();
}

} // namespace Tina::Runtime::Detail
