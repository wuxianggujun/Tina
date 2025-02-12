//
// Created by wuxianggujun on 2025/2/12.
//

#include "tina/core/Timer.hpp"

namespace Tina
{
    Timer::Timer(bool doReset)
    {
        if (doReset)
        {
            reset();
        }
    }

    void Timer::reset()
    {
        m_startTime = Clock::now();
    }

    int64_t Timer::getMicroseconds() const
    {
        auto currentTime = Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_startTime).count();
        if (duration < 0)
        {
            duration = 0;
        }
        return duration;
    }

    int64_t Timer::getMilliseconds() const
    {
        auto currentTime = Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_startTime).count();
        if (duration < 0)
        {
            duration = 0;
        }
        return duration;
    }

    float Timer::getSeconds(bool highPrecision) const
    {
        if (highPrecision)
        {
            int64_t mirco = getMicroseconds();
            return static_cast<float>(mirco) / 1000000.0f;
        }

        int64_t milli = getMilliseconds();
        return static_cast<float>(milli) / 1000.0f;
    }
} // Tina
