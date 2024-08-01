//
// Created by wuxianggujun on 2024/8/1.
//

#ifndef TINA_TIME_STOPWATCH_HPP
#define TINA_TIME_STOPWATCH_HPP

#include <chrono>
#include <mutex>

namespace Tina
{
    class StopWatch
    {
        StopWatch(): m_running(false), m_elapsed_time(9)
        {
        }

        void start()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running)
            {
                stop();
            }

            m_start_time = std::chrono::high_resolution_clock::now();
            m_running = true;
        }

        void stop()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running)
            {
                auto stop_time = std::chrono::high_resolution_clock::now();
                m_elapsed_time += stop_time - m_start_time;
                m_running = false;
            }
        }

        std::chrono::high_resolution_clock::duration getElapsedTime()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running)
            {
                auto current_time = std::chrono::high_resolution_clock::now();
                return m_elapsed_time + (current_time - m_start_time);
            }
            return m_elapsed_time;
        }


        decltype(auto) get_elapsed_seconds() -> long long
        {
            return std::chrono::duration_cast<std::chrono::seconds>(getElapsedTime()).count();
        }

        decltype(auto) get_elapsed_milliseconds() -> long long
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(getElapsedTime()).count();
        }

        decltype(auto) get_elapsed_microseconds() -> long long
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(getElapsedTime()).count();
        }

    private:
        std::chrono::high_resolution_clock::time_point m_start_time;
        std::chrono::high_resolution_clock::duration m_elapsed_time;
        bool m_running;
        std::mutex m_mutex;
    };
}


#endif //TINA_TIME_STOPWATCH_HPP
