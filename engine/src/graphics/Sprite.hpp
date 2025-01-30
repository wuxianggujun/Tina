//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once

#include "Texture.hpp"
#include "math/Rect.hpp"
#include "Color.hpp"

namespace Tina
{
    class Sprite
    {
    public:
        Sprite();
        explicit Sprite(const Texture& texture);

        // 纹理相关
        void setTexture(const Texture& texture);
        void setTextureRect(const Rectf& rect);
        const Texture& getTexture() const;
        const Rectf& getTextureRect() const;

        // 变换相关
        void setPosition(const Vector2f& position);
        void setRotation(float rotation);
        void setScale(const Vector2f& scale);
        void setOrigin(const Vector2f& origin);
        void setColor(const Color& color);

        const Vector2f& getPosition() const;
        float getRotation() const;
        const Vector2f& getScale() const;
        const Vector2f& getOrigin() const;
        const Color& getColor() const;

        // 变换操作
        void move(const Vector2f& offset);
        void rotate(float angle);
        void scale(const Vector2f& factor);

        // 获取全局边界
        Rectf getGlobalBounds() const;
        Rectf getLocalBounds() const;

    private:
        Texture m_texture;
        Rectf m_textureRect;
        Vector2f m_position;
        Vector2f m_scale;
        Vector2f m_origin;
        float m_rotation;
        Color m_color;
    };
} // Tina
