#pragma once

#include <tina/core/base/Compiler.hpp>

#include <chrono>

namespace Tina::Core {

using Duration = std::chrono::duration<double>;
using MonotonicNativeClock = std::chrono::steady_clock;
using MonotonicDuration = MonotonicNativeClock::duration;
using MonotonicTimePoint = MonotonicNativeClock::time_point;

[[nodiscard]] constexpr Duration toDuration(MonotonicDuration duration) noexcept
{
    return std::chrono::duration_cast<Duration>(duration);
}

[[nodiscard]] constexpr Duration durationBetween(
    MonotonicTimePoint begin,
    MonotonicTimePoint end) noexcept
{
    return toDuration(end - begin);
}

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual MonotonicTimePoint now() const noexcept = 0;
};

class TINA_CORE_API SteadyMonotonicClock final : public IMonotonicClock {
public:
    [[nodiscard]] MonotonicTimePoint now() const noexcept override;
};

} // namespace Tina::Core
