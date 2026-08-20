#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UIContext.hpp>

#include <optional>
#include <span>

namespace Tina::Runtime::Detail {

// Runtime-private bridge from backend-neutral Platform preference events to the
// primary UIContext. Events are staged during poll and applied only on the
// Runtime owner thread inside the UI Update phase.
class PrimaryWindowUIColorSchemeCoordinator final {
  public:
    void observe(std::span<const Platform::PlatformEvent> events) noexcept;
    [[nodiscard]] Core::Status apply(UI::UIContext* context);

  private:
    std::optional<UI::UIColorScheme> pendingColorScheme_{};
};

} // namespace Tina::Runtime::Detail
