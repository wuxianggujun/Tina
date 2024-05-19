//
// Created by wuxianggujun on 2024/5/19.
//

#ifndef TINA_TIME_CLOCK_HPP
#define TINA_TIME_CLOCK_HPP

#include <chrono>
#include <filesystem>

namespace Tina {

    class Clock {

    public:
        [[maybe_unused]] static long long FileTimePointToTimeStamp(std::filesystem::file_time_type fileTime);

        [[maybe_unused]] static std::filesystem::file_time_type TimeStampToFileTimePoint(long long timeStamp);

    public:
        Clock();

        Clock(const Clock &) = default;

        Clock &operator=(const Clock &) = default;

        Clock(Clock &&) = default;

        Clock &operator=(Clock &&) = default;

        void update();

        [[maybe_unused]] [[nodiscard]] float getFramerate() const { return 1.0f / m_deltaTime; }

        [[maybe_unused]] [[nodiscard]] float getDeltaTime() const { return m_deltaTime; }

    private:
        float m_deltaTime = 0.0f;
        std::chrono::duration<float> m_elapsedTime;
        std::chrono::steady_clock::time_point m_startTime;
        std::chrono::steady_clock::time_point m_lastTime;
        std::chrono::steady_clock::time_point m_currentTime;
    };

} // Tina

#endif //TINA_TIME_CLOCK_HPP
