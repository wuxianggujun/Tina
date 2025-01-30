//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once
#include "graphics/Sprite.hpp"
#include "graphics/RenderLayer.hpp"

namespace Tina
{
    struct SpriteComponent
    {
        Sprite sprite;
        bool visible{true};
        float depth{getDepthFromLayer(RenderLayer::Default)}; // 默认使用 Default 层
        RenderLayer layer{RenderLayer::Default}; // 当前层级

        void setTexture(const Texture& texture)
        {
            sprite.setTexture(texture);
        }

        void setTextureRect(const Rectf& rect)
        {
            sprite.setTextureRect(rect);
        }

        void setColor(const Color& color)
        {
            sprite.setColor(color);
        }

        // 设置预定义层级
        void setLayer(RenderLayer newLayer)
        {
            layer = newLayer;
            depth = getDepthFromLayer(newLayer);
        }

        // 设置自定义深度值（0.0f - 1.0f）
        void setCustomDepth(float newDepth)
        {
            depth = std::clamp(newDepth, 0.0f, 1.0f);
        }

        // 在两个层级之间设置自定义深度
        void setDepthBetweenLayers(RenderLayer lowerLayer, RenderLayer upperLayer, float t)
        {
            depth = getCustomDepthBetweenLayers(lowerLayer, upperLayer, t);
        }

        // 获取当前深度值
        float getDepth() const { return depth; }

        // 获取当前层级
        RenderLayer getLayer() const { return layer; }
    };
}
