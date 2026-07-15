//
// DayNight - 昼夜循环控制器
// - 以连续时间驱动：白天/夜晚各持续一段时间，支持平滑过渡
// - 提供覆盖层 Alpha，用于在 UI 视图绘制黑色半透明遮罩实现“黑夜效果”

#pragma once

namespace Tina::Game {

struct DayNightConfig {
    float dayLengthSec = 600.0f;      // 白天时长（秒）
    float nightLengthSec = 600.0f;    // 夜晚时长（秒）
    float transitionSec = 60.0f;      // 过渡时长（秒），日落/日出均使用
    float nightMaxAlpha = 0.6f;       // 夜晚最大遮罩强度（0.0~1.0）
};

class DayNight {
public:
    explicit DayNight(const DayNightConfig& cfg = DayNightConfig{})
        : m_cfg(cfg) {}

    // 推进时间（秒）
    void update(float dt) {
        const float cycle = m_cfg.dayLengthSec + m_cfg.nightLengthSec;
        if (cycle <= 0.0f) return;
        m_time = wrap01(m_time + dt / cycle); // 归一化到 [0,1)
    }

    // 获取遮罩透明度（0.0 白天完全透明；夜晚接近 nightMaxAlpha）
    float overlayAlpha() const {
        const float D = m_cfg.dayLengthSec;
        const float N = m_cfg.nightLengthSec;
        const float T = clamp(m_cfg.transitionSec, 0.0f, 0.5f * (D > 0 && N > 0 ? (D < N ? D : N) : 0.0f));
        const float cycle = D + N;
        if (cycle <= 0.0f) return 0.0f;
        float t = m_time * cycle; // 映射回 [0, D+N)

        // 分段：
        // [0, D-T): 白天全亮
        // [D-T, D): 日落过渡 0 -> nightMax
        // [D, D+N-T): 夜晚全暗
        // [D+N-T, D+N): 日出过渡 nightMax -> 0
        if (t < D - T) {
            return 0.0f;
        } else if (t < D) {
            float k = (t - (D - T)) / (T > 0.0f ? T : 1.0f);
            return clamp(k, 0.0f, 1.0f) * m_cfg.nightMaxAlpha;
        } else if (t < D + N - T) {
            return m_cfg.nightMaxAlpha;
        } else if (t < D + N) {
            float k = 1.0f - (t - (D + N - T)) / (T > 0.0f ? T : 1.0f);
            return clamp(k, 0.0f, 1.0f) * m_cfg.nightMaxAlpha;
        }
        return 0.0f;
    }

    // 是否处于夜晚主段（不含过渡段）
    bool isNight() const {
        const float D = m_cfg.dayLengthSec;
        const float N = m_cfg.nightLengthSec;
        const float T = m_cfg.transitionSec;
        const float cycle = D + N;
        if (cycle <= 0.0f) return false;
        float t = m_time * cycle;
        return (t >= D && t < D + N - T);
    }

    // 返回归一化的循环进度 [0,1)
    float normalizedTime() const { return m_time; }

    void setNormalizedTime(float x) { m_time = wrap01(x); }

private:
    static float clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
    static float wrap01(float v) {
        while (v >= 1.0f) v -= 1.0f;
        while (v < 0.0f) v += 1.0f;
        return v;
    }

    DayNightConfig m_cfg{};
    float m_time = 0.0f; // 归一化时间 [0,1)
};

} // namespace Tina::Game

