#pragma once

#include <tina/core/base/Types.hpp>

#include <memory>

namespace Tina::UI::Detail {

class UIContextLifetimeControl;

} // namespace Tina::UI::Detail

namespace Tina::UI {

class UIContext;
class UIInputRouter;
class UITreeUpdater;

// Move-only ownership of one routed-pointer listener registration. Owner-thread
// reset takes effect immediately, including during dispatch. Off-thread reset is
// queued in bounded storage and takes effect before the next owner-thread UI
// mutation or route. Reset remains safe after the UIContext is destroyed.
class UIRoutedPointerListenerToken final {
  public:
    UIRoutedPointerListenerToken() noexcept = default;
    ~UIRoutedPointerListenerToken() noexcept;

    UIRoutedPointerListenerToken(const UIRoutedPointerListenerToken&) = delete;
    UIRoutedPointerListenerToken& operator=(const UIRoutedPointerListenerToken&) = delete;

    UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept;
    UIRoutedPointerListenerToken& operator=(UIRoutedPointerListenerToken&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class UIContext;
    friend class UIInputRouter;
    friend class UITreeUpdater;

    UIRoutedPointerListenerToken(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, u32 slot,
                                 u32 generation) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    u32 m_slot = 0;
    u32 m_generation = 0;
};


} // namespace Tina::UI
