#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/InputRouting.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIFocus.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <vector>

namespace Tina::Runtime::Input {

struct UIInputRouteOutputView final {
    UI::InputTransitionConsumptionView consumption{};
    UI::ContinuousControlClaimsView claims{};
};

// Runtime-private bridge from one validated Platform frame to the vNext UI
// pointer router. Returned spans borrow producer-owned storage until the next
// successful produce() call. Failed routing keeps the last successfully
// published view but consumes the attempted frame/sequence watermark: retrying
// that same frame is rejected because earlier listener side effects cannot be
// rolled back safely. Pointer-button requests are deduplicated and published
// only while the final Platform snapshot still holds the control. Consumption
// words and claims use Create-time bounded double buffers; a custom memory
// resource must outlive the producer.
class UIInputRouteProducer final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<UIInputRouteProducer>>
    Create(usize rawTransitionCapacity, usize continuousControlClaimCapacity,
           std::pmr::memory_resource& memoryResource = *std::pmr::get_default_resource());

    UIInputRouteProducer(const UIInputRouteProducer&) = delete;
    UIInputRouteProducer& operator=(const UIInputRouteProducer&) = delete;
    UIInputRouteProducer(UIInputRouteProducer&&) = delete;
    UIInputRouteProducer& operator=(UIInputRouteProducer&&) = delete;

    [[nodiscard]] Core::Result<UIInputRouteOutputView> produce(UI::UIContext* context,
                                                               const Platform::PlatformFrameView& platformFrame);

  private:
    UIInputRouteProducer(usize rawTransitionCapacity, usize continuousControlClaimCapacity,
                         std::pmr::vector<u64> publishedWords, std::pmr::vector<u64> stagingWords,
                         std::pmr::vector<UI::ContinuousControlClaim> publishedClaims,
                         std::pmr::vector<UI::ContinuousControlClaim> stagingClaims) noexcept;

    [[nodiscard]] Core::Status preflight(const UI::UIContext* context,
                                         const Platform::PlatformFrameView& platformFrame) const;

    // Analog sticks report a continuous value while focus navigation is
    // edge-triggered, so a held stick would otherwise move focus every frame.
    // One latch per gamepad slot and axis pair: the stick must return to neutral
    // before it can step focus again, and two pads latch independently.
    struct StickNavigationLatch final {
        Platform::GamepadId gamepad{};
        // Direction currently held past the step threshold, or none while neutral.
        std::optional<UI::UIFocusNavigationDirection> horizontal{};
        std::optional<UI::UIFocusNavigationDirection> vertical{};
    };

    [[nodiscard]] StickNavigationLatch& stickLatchFor(Platform::GamepadId gamepad) noexcept;

    usize rawTransitionCapacity_ = 0;
    usize continuousControlClaimCapacity_ = 0;
    std::array<StickNavigationLatch, Platform::PlatformFrameBuilder::MaximumGamepadSlots>
        stickLatches_{};
    std::pmr::vector<u64> publishedWords_;
    std::pmr::vector<u64> stagingWords_;
    std::pmr::vector<UI::ContinuousControlClaim> publishedClaims_;
    std::pmr::vector<UI::ContinuousControlClaim> stagingClaims_;
    std::thread::id ownerThreadId_{};
    std::optional<Platform::PlatformFrameId> lastAttemptedPlatformFrame_;
    std::optional<u64> lastAttemptedRawSequence_;
    bool producing_ = false;
};

} // namespace Tina::Runtime::Input
