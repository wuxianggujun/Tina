#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>

#include <span>
#include <variant>

namespace Tina::UI {

// Frame-local UI route output. Empty words mean no transition was consumed;
// otherwise the words are indexed by raw PlatformFrame transition ordinal.
// The view borrows its route-result producer's frame storage and must not
// outlive that routing frame.
struct InputTransitionConsumptionView final {
    Platform::PlatformFrameId platformFrame{};
    usize transitionCount = 0;
    std::span<const u64> consumedOrdinalWords{};

    [[nodiscard]] static InputTransitionConsumptionView None(
        Platform::PlatformFrameId frame,
        usize count) noexcept
    {
        return InputTransitionConsumptionView{
            .platformFrame = frame,
            .transitionCount = count,
        };
    }

    [[nodiscard]] bool isConsumed(usize ordinal) const noexcept
    {
        if (ordinal >= transitionCount || consumedOrdinalWords.empty()) {
            return false;
        }
        constexpr usize BitsPerWord = sizeof(u64) * 8;
        const usize word = ordinal / BitsPerWord;
        const usize bit = ordinal % BitsPerWord;
        return word < consumedOrdinalWords.size()
            && (consumedOrdinalWords[word] & (u64{1} << bit)) != 0;
    }
};

using ClaimedInputControlIdentity =
    std::variant<Platform::KeyControlIdentity, Platform::PointerButtonControlIdentity,
                 Platform::GamepadButtonControlIdentity, Platform::GamepadAxisControlIdentity,
                 Platform::PointerContinuousControlIdentity>;

struct ContinuousControlClaim final {
    ClaimedInputControlIdentity control{};
};

// A claim owns its control for this complete mapping frame. Cross-frame
// suppression remains Runtime ActionMapper state and is never written back to
// Platform snapshots.
struct ContinuousControlClaimsView final {
    Platform::PlatformFrameId platformFrame{};
    std::span<const ContinuousControlClaim> controls{};

    [[nodiscard]] static ContinuousControlClaimsView None(Platform::PlatformFrameId frame) noexcept
    {
        return ContinuousControlClaimsView{.platformFrame = frame};
    }
};

} // namespace Tina::UI
