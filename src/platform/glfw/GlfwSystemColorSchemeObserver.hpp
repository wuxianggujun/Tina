#pragma once

#include <tina/platform/PlatformFrame.hpp>

#include <optional>
#include <span>

namespace Tina::Platform::Detail {

using GlfwSystemColorSchemeQuery = std::optional<SystemColorScheme> (*)() noexcept;

[[nodiscard]] std::optional<SystemColorScheme> queryHostSystemColorScheme() noexcept;

class GlfwSystemColorSchemeObserver final {
  public:
    explicit GlfwSystemColorSchemeObserver(
        bool enabled,
        GlfwSystemColorSchemeQuery query = queryHostSystemColorScheme) noexcept;

    [[nodiscard]] std::optional<SystemColorScheme> pendingPreference() const noexcept;
    void commitPublishedPreference(SystemColorScheme colorScheme,
                                   std::span<const PlatformEvent> publishedEvents) noexcept;

  private:
    GlfwSystemColorSchemeQuery query_ = nullptr;
    std::optional<SystemColorScheme> publishedPreference_{};
    bool enabled_ = false;
};

} // namespace Tina::Platform::Detail
