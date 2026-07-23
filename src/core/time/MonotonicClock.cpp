#include <tina/core/time/MonotonicClock.hpp>

namespace Tina::Core {

MonotonicTimePoint SteadyMonotonicClock::now() const noexcept
{
    return MonotonicNativeClock::now();
}

} // namespace Tina::Core
