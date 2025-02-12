//
// Created by wuxianggujun on 2025/2/12.
//

#pragma once

#include <chrono>
#include <cstdint>

namespace Tina
{
    class Timer
    {
    public:
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = Clock::time_point;

        /**
         *  @brief 构造函数
         * @param doReset 是否在构造时重置计时器
         */
        explicit Timer(bool doReset = true);
        /**
         *  @brief 重置计时器
         */
        void reset();
        /**
         * @brief 获取从计时器启动到当前时间的微秒数
         * @return 计时器经过的时间（微秒）
         */
        [[nodiscard]] int64_t getMicroseconds() const;

        /**
         * @brief 获取从计时器启动到当前时间的毫秒数
         * @return 计时器经过的时间（毫秒）
         */
        [[nodiscard]] int64_t getMilliseconds() const;

        /**
         * @brief 获取从计时器启动到当前时间的秒数
         * @param highPrecision  是否使用高精度计时器
         * @return 秒数
         */
        float getSeconds(bool highPrecision = false) const;

    private:
        TimePoint m_startTime;
    };
} // Tina
