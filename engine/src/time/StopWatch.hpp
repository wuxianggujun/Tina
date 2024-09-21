//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/CrikeeIP/Stopwatch
//

#ifndef TINA_TIME_STOPWATCH_HPP
#define TINA_TIME_STOPWATCH_HPP

#include <chrono>
#include <string>
#include <format>

namespace Tina
{
    class Stopwatch
    {
    public:
        enum TimeFormat
        {
            NANOSECONDS, MICROSECONDS, MILLISECONDS, SECONDS, MINUTES, HOURS, DAYS
        };

        Stopwatch() : start_time({}), stop_time({}), stopped(false)
        {
        }

        void start()
        {
            start_time = clock::now();
        }

        void stop()
        {
            stop_time = clock::now();
            m_duration = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
        }

        decltype(auto) duration() const
        {
            return m_duration;
        }

        template <TimeFormat fmt = MILLISECONDS, class Func>
        decltype(auto) time(Func&& func)
        {
            start();
            func();
            stop();
            return duration();
        }

        static std::string format(std::chrono::microseconds::rep duration)
        {
            using namespace std::chrono;

            auto ms = duration_cast<milliseconds>(microseconds(duration)).count();
            auto s = duration_cast<seconds>(microseconds(duration)).count();
            auto m = duration_cast<minutes>(microseconds(duration)).count();
            auto h = duration_cast<hours>(microseconds(duration)).count();
            auto d = duration_cast<days>(microseconds(duration)).count();
            // 使用std::format来格式化字符串
            if (d > 0)
            {
                return std::format("{}d {}h {}m {}s {}ms", d, h % 24, m % 60, s % 60, ms % 1000);
            }
            if (h > 0)
            {
                return std::format("{}h {}m {}s {}ms", h, m % 60, s % 60, ms);
            }
            if (m > 0)
            {
                return std::format("{}m {}s {}ms", m, s % 60, ms);
            }
            if (s > 0)
            {
                return std::format("{}s {}ms", s, ms);
            }
            return std::format("{}ms", ms);
        }

    private:
        using clock = std::chrono::high_resolution_clock;
        using time_pt = std::chrono::time_point<std::chrono::high_resolution_clock>;
        time_pt start_time;
        time_pt stop_time;
        std::chrono::microseconds::rep m_duration{};

        mutable bool stopped;
    };
}


#endif //TINA_TIME_STOPWATCH_HPP
