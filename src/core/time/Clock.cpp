#include "Clock.hpp"

namespace Tina::Core {

MonotonicTimePoint SteadyMonotonicClock::now() const noexcept
{
    return MonotonicNativeClock::now();
}

Clock::TimePoint Clock::now() noexcept
{
    return NativeClock::now();
}

i64 Clock::ticks() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now().time_since_epoch()).count();
}

double Clock::secondsBetween(TimePoint begin, TimePoint end) noexcept
{
    return std::chrono::duration<double>(end - begin).count();
}

} // namespace Tina::Core
