#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformBackend.hpp>

#include <thread>

namespace Tina::UI {
class UIContext;
}

namespace Tina::Runtime::Detail {

// Publishes the caret from the last successful UI paint commit to the primary
// Platform backend. The coordinator owns no UI or native state; it only enforces
// the Runtime phase boundary and translates Tina-owned geometry.
class PrimaryWindowTextInputPlacementCoordinator final {
  public:
    PrimaryWindowTextInputPlacementCoordinator() noexcept;

    PrimaryWindowTextInputPlacementCoordinator(const PrimaryWindowTextInputPlacementCoordinator&) = delete;
    PrimaryWindowTextInputPlacementCoordinator& operator=(const PrimaryWindowTextInputPlacementCoordinator&) = delete;
    PrimaryWindowTextInputPlacementCoordinator(PrimaryWindowTextInputPlacementCoordinator&&) = delete;
    PrimaryWindowTextInputPlacementCoordinator& operator=(PrimaryWindowTextInputPlacementCoordinator&&) = delete;

    [[nodiscard]] Core::Status publish(UI::UIContext* context,
                                       Platform::IPlatformBackend& backend);

  private:
    std::thread::id ownerThreadId_{};
};

} // namespace Tina::Runtime::Detail\n
