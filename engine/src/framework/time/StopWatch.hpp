//
// Created by wuxianggujun on 2024/6/28.
// 借鉴项目 https://github.com/sailormoon/stopwatch/blob/master/include/stopwatch.h
//

#ifndef TINA_TIME_STOPWATCH_HPP
#define TINA_TIME_STOPWATCH_HPP

#include <chrono>
#include <algorithm>
#include <array>
#include <cstdint>
#include <ratio>

namespace Tina::StopWatch {
    using steady_clock = std::chrono::steady_clock;

    template<class Clock = steady_clock>
    struct timer {
        using time_point = typename Clock::time_point;
        using duration = typename Clock::duration;

        explicit timer(const duration duration) noexcept : expiry(Clock::now() + duration) {
        }

        explicit timer(const time_point expiry) noexcept : expiry(expiry) {
        }

        bool done(time_point now = Clock::now()) const noexcept {
            return now >= expiry;
        }

        auto remaining(time_point now = Clock::now()) const noexcept -> duration {
            return expiry - now;
        }

        const time_point expiry;;
    };

    template<class Clock = steady_clock>
    constexpr auto make_timer(typename Clock::duration duration) -> timer<Clock> {
        return timer<Clock>(duration);
    }

    template<class Clock = steady_clock, class Func>
    auto time(Func &&function) -> typename Clock::duration {
        const auto start = Clock::now();
        function();
        return Clock::now() - start;
    }

    template<class duration = steady_clock::duration>
    auto toMilliseconds(duration dur) -> std::chrono::milliseconds::rep {
        return std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
    }

    template<class Clock=steady_clock, class Func>
    auto milliseconds(Func &&function) -> std::chrono::milliseconds::rep {
        const auto start = Clock::now();
        function();
        return toMilliseconds(Clock::now() - start);
    }


    template<std::size_t N, class Clock = steady_clock, class Func>
    auto sample(Func &&function) -> std::array<typename Clock::duration, N> {
        std::array<typename Clock::duration, N> samples;

        for (std::size_t i = 0u; i < N; ++i) {
            samples[i] = time<Clock>(function);
        }

        std::sort(samples.begin(), samples.end());
        return samples;
    }
} // Tina

#endif //TINA_TIME_STOPWATCH_HPP
