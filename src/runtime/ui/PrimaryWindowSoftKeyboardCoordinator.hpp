#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/ui/UINodeId.hpp>

#include <thread>

namespace Tina::UI {
class UIContext;
}

namespace Tina::Runtime::Detail {

// Observes UI TextEdit focus changes and drives the platform soft keyboard
// show/hide request state machine. The coordinator owns no UI or native state;
// it only enforces the Runtime phase boundary and translates focus changes
// to the platform capability API.
//
// A Show request is sent when focus changes from None to a valid TextEdit node.
// A Hide request is sent when focus changes from a valid TextEdit node to None.
// Focus changes between different TextEdit nodes do not toggle the keyboard.
class PrimaryWindowSoftKeyboardCoordinator final {
  public:
    PrimaryWindowSoftKeyboardCoordinator() noexcept;

    PrimaryWindowSoftKeyboardCoordinator(const PrimaryWindowSoftKeyboardCoordinator&) = delete;
    PrimaryWindowSoftKeyboardCoordinator& operator=(const PrimaryWindowSoftKeyboardCoordinator&) = delete;
    PrimaryWindowSoftKeyboardCoordinator(PrimaryWindowSoftKeyboardCoordinator&&) = delete;
    PrimaryWindowSoftKeyboardCoordinator& operator=(PrimaryWindowSoftKeyboardCoordinator&&) = delete;

    // Observes the committed UI focus state and requests soft keyboard show/hide
    // when focus transitions between None and a valid TextEdit node. Must be
    // called after UI publication commits focus changes.
    [[nodiscard]] Core::Status publish(UI::UIContext* context,
                                       Platform::IPlatformBackend& backend);

  private:
    UI::UINodeId previousTextInputFocus_{};
    std::thread::id ownerThreadId_{};
};

} // namespace Tina::Runtime::Detail
