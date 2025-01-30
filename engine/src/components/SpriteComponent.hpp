//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once
#include "graphics/Sprite.hpp"

namespace Tina
{
    struct SpriteComponent
    {
        Sprite sprite;
        bool visible{true};
        float depth{0.0f};

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
    };
}
