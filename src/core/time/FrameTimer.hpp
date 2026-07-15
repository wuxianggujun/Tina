#pragma once

#include "Clock.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <functional>
#include <utility>

namespace Tina::Core {

struct TimeConfig {
    double time_scale = 1.0;
    double dt_min = 0.0;
    double dt_max = 1.0 / 20.0;
    int tick_rate = 60;
    int max_substeps = 4;
    int frame_cap_hz = 0;
};

class FrameTimer final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FrameTimer() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        m_start = m_previous = Clock::now();
        m_deltaSeconds = 0.0;
        m_frameSeconds = 0.0;
        m_fpsAccumulator = 0.0;
        m_fps = 0.0;
        m_frameCount = 0;
    }

    void beginFrame() noexcept
    {
        const TimePoint current = Clock::now();
        m_frameSeconds = std::chrono::duration<double>(current - m_previous).count();
        m_previous = current;
        m_deltaSeconds = m_frameSeconds;

        m_fpsAccumulator += m_frameSeconds;
        ++m_frameCount;
        if (m_fpsAccumulator >= 1.0) {
            m_fps = static_cast<double>(m_frameCount) / m_fpsAccumulator;
            m_frameCount = 0;
            m_fpsAccumulator = 0.0;
        }
    }

    [[nodiscard]] double deltaSeconds() const noexcept { return m_deltaSeconds; }
    [[nodiscard]] double frameSeconds() const noexcept { return m_frameSeconds; }
    [[nodiscard]] double fps() const noexcept { return m_fps; }
    [[nodiscard]] double sinceStartupSeconds() const noexcept
    {
        return std::chrono::duration<double>(Clock::now() - m_start).count();
    }

private:
    TimePoint m_start{};
    TimePoint m_previous{};
    double m_deltaSeconds = 0.0;
    double m_frameSeconds = 0.0;
    double m_fpsAccumulator = 0.0;
    double m_fps = 0.0;
    int m_frameCount = 0;
};

class FixedStepTicker final {
public:
    explicit FixedStepTicker(const TimeConfig& config) noexcept
    {
        reset(config);
    }

    void reset(const TimeConfig& config) noexcept
    {
        m_config = config;
        m_fixedDelta = config.tick_rate > 0 ? 1.0 / static_cast<double>(config.tick_rate) : 0.0;
        m_accumulator = 0.0;
    }

    void accumulate(double deltaSeconds) noexcept
    {
        if (!std::isfinite(deltaSeconds) || !std::isfinite(m_config.time_scale)) {
            return;
        }

        const auto [configuredMinimum, configuredMaximum] =
            std::minmax(m_config.dt_min, m_config.dt_max);
        const double minimum = std::max(0.0, configuredMinimum);
        const double maximum = std::max(minimum, configuredMaximum);
        const double clampedDelta = std::clamp(deltaSeconds, minimum, maximum);
        m_accumulator = std::max(0.0, m_accumulator + clampedDelta * m_config.time_scale);
    }

    template <typename Simulation>
        requires std::invocable<Simulation&, double>
    int step(Simulation&& simulate)
    {
        if (m_fixedDelta <= 0.0 || m_config.max_substeps <= 0) {
            m_accumulator = 0.0;
            return 0;
        }

        int stepCount = 0;
        while (m_accumulator + 1e-12 >= m_fixedDelta && stepCount < m_config.max_substeps) {
            m_accumulator -= m_fixedDelta;
            ++stepCount;
            std::invoke(simulate, m_fixedDelta);
        }

        if (stepCount == m_config.max_substeps && m_accumulator > m_fixedDelta * 2.0) {
            m_accumulator = 0.0;
        }
        return stepCount;
    }

    [[nodiscard]] double alpha() const noexcept
    {
        return m_fixedDelta > 0.0 ? std::clamp(m_accumulator / m_fixedDelta, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] double fixedDelta() const noexcept { return m_fixedDelta; }
    [[nodiscard]] const TimeConfig& config() const noexcept { return m_config; }

private:
    TimeConfig m_config{};
    double m_fixedDelta = 0.0;
    double m_accumulator = 0.0;
};

} // namespace Tina::Core
