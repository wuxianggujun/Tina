#pragma once

#include <algorithm>

namespace Tina::UI {

/**
 * UI工具类 - 提供UI相关的通用功能
 */
class UIUtils {
public:
    /**
     * UI缩放配置
     */
    struct ScaleConfig {
        float minScale = 0.6f;      // 最小缩放比例
        float maxScale = 1.5f;      // 最大缩放比例
        int baseWidth = 1280;        // 基准宽度
        int baseHeight = 720;        // 基准高度
        int largeWidth = 1920;       // 大屏幕宽度阈值
        int largeHeight = 1080;      // 大屏幕高度阈值
        bool maintainAspectRatio = true;  // 保持宽高比
    };

    /**
     * 计算UI缩放比例
     * 根据窗口大小自动计算合适的UI缩放比例
     *
     * @param windowWidth 窗口宽度
     * @param windowHeight 窗口高度
     * @param config 缩放配置（可选）
     * @return 计算得出的缩放比例
     */
    static float calculateUIScale(int windowWidth, int windowHeight,
                                 const ScaleConfig& config = ScaleConfig{}) {
        float scale = 1.0f;

        // 小于基准尺寸时缩小
        if (windowWidth < config.baseWidth || windowHeight < config.baseHeight) {
            float scaleX = (float)windowWidth / config.baseWidth;
            float scaleY = (float)windowHeight / config.baseHeight;

            if (config.maintainAspectRatio) {
                scale = std::min(scaleX, scaleY);
            } else {
                scale = (scaleX + scaleY) * 0.5f;
            }

            scale = std::max(scale, config.minScale);
        }
        // 大于大屏幕阈值时放大
        else if (windowWidth > config.largeWidth || windowHeight > config.largeHeight) {
            float scaleX = (float)windowWidth / config.largeWidth;
            float scaleY = (float)windowHeight / config.largeHeight;

            if (config.maintainAspectRatio) {
                scale = std::min(scaleX, scaleY);
            } else {
                scale = (scaleX + scaleY) * 0.5f;
            }

            scale = std::min(scale, config.maxScale);
        }

        return scale;
    }

    /**
     * 计算居中位置
     *
     * @param containerWidth 容器宽度
     * @param containerHeight 容器高度
     * @param itemWidth 项目宽度
     * @param itemHeight 项目高度
     * @return 居中位置的x, y坐标（作为pair返回）
     */
    static std::pair<float, float> calculateCenterPosition(
        float containerWidth, float containerHeight,
        float itemWidth, float itemHeight) {
        float x = (containerWidth - itemWidth) * 0.5f;
        float y = (containerHeight - itemHeight) * 0.5f;
        return {x, y};
    }

    /**
     * 应用缩放到尺寸值
     *
     * @param baseSize 基础尺寸
     * @param scale 缩放比例
     * @return 缩放后的尺寸
     */
    static float applyScale(float baseSize, float scale) {
        return baseSize * scale;
    }
};

} // namespace Tina::UI