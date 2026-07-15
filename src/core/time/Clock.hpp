#pragma once

#include "../base/Compiler.hpp"
#include "../base/Types.hpp"

#include <chrono>

namespace Tina::Core {

class TINA_CORE_API Clock final {
public:
    using NativeClock = std::chrono::steady_clock;
    using TimePoint = NativeClock::time_point;
    using Duration = NativeClock::duration;

    [[nodiscard]] static TimePoint now() noexcept;
    [[nodiscard]] static i64 ticks() noexcept;
    [[nodiscard]] static double secondsBetween(TimePoint begin, TimePoint end) noexcept;
};

} // namespace Tina::Core
