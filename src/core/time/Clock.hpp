#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>

namespace Tina::Core {

// Legacy compatibility facade. vNext code receives IMonotonicClock through construction.
class TINA_CORE_API Clock final {
public:
    using NativeClock = MonotonicNativeClock;
    using TimePoint = MonotonicTimePoint;
    using Duration = MonotonicDuration;

    [[nodiscard]] static TimePoint now() noexcept;
    [[nodiscard]] static i64 ticks() noexcept;
    [[nodiscard]] static double secondsBetween(TimePoint begin, TimePoint end) noexcept;
};

} // namespace Tina::Core
