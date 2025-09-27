//
// 帧计时与固定逻辑步（基于 docs/frame_timing.md）
// - FrameTimer: 高精度帧计时，提供 dt、fps、可选限帧 sleep
// - FixedStepTicker: 逻辑固定步累加器（accumulator），限制最大子步并返回插值 alpha

#pragma once

#include "Core.hpp"
#include <chrono>

namespace Tina::Core {

struct TimeConfig {
    double time_scale = 1.0;        // 时间倍率
    double dt_min = 1.0 / 120.0;    // 可变 dt 下限（秒）
    double dt_max = 1.0 / 20.0;     // 可变 dt 上限（秒）
    int tick_rate = 60;             // 固定逻辑步频率（Hz）
    int max_substeps = 4;           // 每帧最多固定步次数
    int frame_cap_hz = 0;           // 渲染限帧（0=关闭）
};

class FrameTimer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void reset() {
        m_start = m_prev = Clock::now();
        m_dt = 0.0;
        m_frame_time = 0.0;
        m_frames = 0;
        m_fps_acc = 0.0;
        m_fps = 0.0;
    }

    // 在每帧开头调用，计算上一帧耗时
    void beginFrame() {
        const TimePoint now = Clock::now();
        m_frame_time = std::chrono::duration<double>(now - m_prev).count();
        m_prev = now;
        m_dt = m_frame_time; // 原始可变 dt

        // FPS 统计（1 秒窗口）
        m_fps_acc += m_frame_time;
        ++m_frames;
        if (m_fps_acc >= 1.0) {
            m_fps = m_frames / m_fps_acc;
            m_frames = 0;
            m_fps_acc = 0.0;
        }
    }

    // dt（秒）
    double deltaSeconds() const { return m_dt; }
    double frameSeconds() const { return m_frame_time; }
    double fps() const { return m_fps; }
    double sinceStartupSeconds() const { return std::chrono::duration<double>(Clock::now() - m_start).count(); }

private:
    TimePoint m_start = Clock::now();
    TimePoint m_prev = m_start;
    double m_dt = 0.0;
    double m_frame_time = 0.0;
    int m_frames = 0;
    double m_fps_acc = 0.0;
    double m_fps = 0.0;
};

class FixedStepTicker {
public:
    explicit FixedStepTicker(const TimeConfig& cfg) { reset(cfg); }

    void reset(const TimeConfig& cfg) {
        m_cfg = cfg;
        m_fixed_dt = (cfg.tick_rate > 0) ? 1.0 / double(cfg.tick_rate) : 0.0;
        m_accumulator = 0.0;
    }

    // 累加一帧的可变 dt（已做倍率与钳制）
    void accumulate(double dt) {
        if (dt < m_cfg.dt_min) dt = m_cfg.dt_min;
        if (dt > m_cfg.dt_max) dt = m_cfg.dt_max;
        m_accumulator += dt * m_cfg.time_scale;
    }

    // 执行固定步更新，返回消耗的步数；用户在回调中做 simulate(fixed_dt)
    template <typename F>
    int step(F&& simulate) {
        int steps = 0;
        while (m_accumulator + 1e-12 >= m_fixed_dt && steps < m_cfg.max_substeps) {
            simulate(m_fixed_dt);
            m_accumulator -= m_fixed_dt;
            ++steps;
        }
        // 超过上限：丢弃多余累积，避免死亡螺旋
        if (steps >= m_cfg.max_substeps && m_accumulator > m_fixed_dt * 2) {
            m_accumulator = 0.0;
        }
        return steps;
    }

    // 渲染插值因子 [0,1)
    double alpha() const { return (m_fixed_dt > 0) ? (m_accumulator / m_fixed_dt) : 0.0; }

    double fixedDelta() const { return m_fixed_dt; }
    const TimeConfig& config() const { return m_cfg; }

private:
    TimeConfig m_cfg{};
    double m_fixed_dt = 0.0;
    double m_accumulator = 0.0;
};

} // namespace Tina::Core

