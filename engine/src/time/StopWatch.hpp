//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/CrikeeIP/Stopwatch
//

#ifndef TINA_TIME_STOPWATCH_HPP
#define TINA_TIME_STOPWATCH_HPP

#include <chrono>

namespace Tina
{
    class Stopwatch
    {
    public:
        enum TimeFormat { NANOSECONDS, MICROSECONDS, MILLISECONDS, SECONDS };

        Stopwatch() : stopped(false)
        {
            start();
        }

        ~Stopwatch()
        {
            if (!stopped)
            {
                stop();
            }
        }

        void start()
        {
            if (!stopped)
            {
                start_time = std::chrono::high_resolution_clock::now();
            }
            else
            {
                // 如果计时器已经停止，再次开始时重置 stopped 状态
                stopped = false;
            }
        }

        void stop()
        {
            if (!stopped)
            {
                const auto end_time = std::chrono::high_resolution_clock::now();
                elapsed_time = end_time - start_time;
                stopped = true;
            }
        }

        template <TimeFormat fmt = TimeFormat::MILLISECONDS>
        [[nodiscard]] decltype(auto) elapsed() const
        {
            if (!stopped)
            {
                /*const auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = end_time - start_time;*/
                return ticks<fmt>(elapsed_time);
            }
            return ticks<fmt>(elapsed_time);
        }

    private:
        typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_pt;
        time_pt start_time;
        std::chrono::high_resolution_clock::duration elapsed_time{};
        bool stopped;

        template <TimeFormat fmt>
        static decltype(auto) ticks(const std::chrono::high_resolution_clock::duration& duration)
        {
            switch (fmt)
            {
            case NANOSECONDS:
                return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
            case MICROSECONDS:
                return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            case MILLISECONDS:
                return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            case SECONDS:
                return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            }
        }
    };
}


#endif //TINA_TIME_STOPWATCH_HPP
