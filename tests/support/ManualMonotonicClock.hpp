#pragma once

#include <tina/core/time/MonotonicClock.hpp>

#include <chrono>

namespace Tina::Tests {

class ManualMonotonicClock final : public Core::IMonotonicClock {
public:
    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return m_now;
    }

    void advance(Core::Duration duration) noexcept
    {
        m_now += std::chrono::duration_cast<Core::MonotonicDuration>(duration);
    }

private:
    Core::MonotonicTimePoint m_now{};
};

} // namespace Tina::Tests
