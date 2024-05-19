//
// Created by wuxianggujun on 2024/5/19.
//

#include "Clock.hpp"

namespace Tina {

    [[maybe_unused]] long long Clock::FileTimePointToTimeStamp(std::filesystem::file_time_type fileTime) {
        auto systemClock = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::system_clock::to_time_t(systemClock);
    }

    [[maybe_unused]] std::filesystem::file_time_type Clock::TimeStampToFileTimePoint(long long int timeStamp) {
        std::chrono::system_clock::time_point timePoint = std::chrono::system_clock::from_time_t(
                static_cast<std::time_t>(timeStamp));
        return std::filesystem::file_time_type::clock::now() + (timePoint - std::chrono::system_clock::now());

    }

    Clock::Clock() : m_deltaTime(0.0), m_elapsedTime(), m_startTime(std::chrono::steady_clock::now()),
                     m_lastTime(m_startTime), m_currentTime(m_startTime) {

    }

    void Clock::update() {
        m_lastTime = m_currentTime;
        m_currentTime = std::chrono::steady_clock::now();
        m_elapsedTime = m_currentTime - m_lastTime;
        m_deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(m_elapsedTime).count();
    }
} // Tina