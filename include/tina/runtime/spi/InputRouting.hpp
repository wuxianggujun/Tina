#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActionMap.hpp>

#include <span>
#include <variant>

namespace Tina {

struct InputActionMapperCapacityConfig final {
    static constexpr u32 DefaultContinuousControlClaimCapacity = 64;
    static constexpr u32 MaximumContinuousControlClaimCapacity = 1024;

    u32 rawInputTransitionCapacity = Platform::PlatformFrameCapacityConfig::DefaultInputTransitionCapacity;
    u32 continuousControlClaimCapacity = DefaultContinuousControlClaimCapacity;
    u32 simulationActionTransitionCapacity = InputActionMapCapacityConfig::DefaultSimulationActionTransitionCapacity;
    u32 frameActionTransitionCapacity = InputActionMapCapacityConfig::DefaultFrameActionTransitionCapacity;
    u32 digitalActionBindingCapacity = InputActionMapCapacityConfig::DefaultDigitalActionBindingCapacity;
};

// UI-owned bit storage. Empty words mean "none consumed"; otherwise the span
// contains ceil(transitionCount / 64) words indexed by raw transition ordinal.
// platformFrame must match the PlatformFrameView routed by UI.
struct InputTransitionConsumption final {
    Platform::PlatformFrameId platformFrame{};
    usize transitionCount = 0;
    std::span<const u64> consumedOrdinalWords{};

    [[nodiscard]] static InputTransitionConsumption None(Platform::PlatformFrameId frame, usize count) noexcept
    {
        return InputTransitionConsumption{
            .platformFrame = frame,
            .transitionCount = count,
        };
    }

    [[nodiscard]] bool isConsumed(usize ordinal) const noexcept
    {
        if (ordinal >= transitionCount || consumedOrdinalWords.empty())
        {
            return false;
        }
        constexpr usize BitsPerWord = sizeof(u64) * 8;
        const usize word = ordinal / BitsPerWord;
        const usize bit = ordinal % BitsPerWord;
        return word < consumedOrdinalWords.size() && (consumedOrdinalWords[word] & (u64{1} << bit)) != 0;
    }
};

using ClaimedInputControlIdentity =
    std::variant<Platform::KeyControlIdentity, Platform::PointerButtonControlIdentity,
                 Platform::GamepadButtonControlIdentity, Platform::GamepadAxisControlIdentity,
                 Platform::PointerContinuousControlIdentity>;

struct ContinuousControlClaim final {
    ClaimedInputControlIdentity control{};
};

// Current UI route output only. A claim owns its control for this complete
// mapping frame and is applied before raw transition mapping; it does not begin
// at an individual transition sequence. Cross-frame suppression remains
// exclusively in ActionMapper and is never written back into Platform snapshots.
struct ContinuousControlClaims final {
    Platform::PlatformFrameId platformFrame{};
    std::span<const ContinuousControlClaim> controls{};

    [[nodiscard]] static ContinuousControlClaims None(Platform::PlatformFrameId frame) noexcept
    {
        return ContinuousControlClaims{.platformFrame = frame};
    }
};

} // namespace Tina
