#pragma once

namespace Tina
{
    // 预定义的渲染层级
    enum class RenderLayer
    {
        Background = 0,    // 背景层
        Default = 1,      // 默认层
        EntityLow = 2,    // 实体低层
        EntityMid = 3,    // 实体中层
        EntityHigh = 4,   // 实体高层
        UI = 5,           // UI层
        UIHigh = 6,       // UI高层
        Overlay = 7,      // 覆盖层
        Debug = 8         // 调试层
    };

    // 将枚举转换为浮点深度值
    inline float getDepthFromLayer(RenderLayer layer)
    {
        const int numLayers = 9; // 总层数
        const float layerValue = static_cast<float>(static_cast<int>(layer));
        return layerValue / static_cast<float>(numLayers - 1);
    }

    // 将自定义深度值限制在指定层级之间
    inline float getCustomDepthBetweenLayers(RenderLayer lowerLayer, RenderLayer upperLayer, float t)
    {
        float lowerDepth = getDepthFromLayer(lowerLayer);
        float upperDepth = getDepthFromLayer(upperLayer);
        // 将 t 限制在 0-1 之间
        t = std::max(0.0f, std::min(1.0f, t));
        // 在两层之间进行线性插值
        return lowerDepth + (upperDepth - lowerDepth) * t;
    }
} 