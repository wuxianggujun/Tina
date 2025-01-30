//
// Created by wuxianggujun on 2025/1/30.
//

#include "Sprite.hpp"

namespace Tina
{
    Sprite::Sprite(): m_textureRect(0.0f, 0.0f, 0.0f, 0.0f),
                      m_position(0.0f, 0.0f),
                      m_scale(1.0f, 1.0f), m_origin(0.0f, 0.0f), m_rotation(0.0f), m_color(Color::White)
    {
    }

    Sprite::Sprite(const Texture& texture): Sprite()
    {
        setTexture(texture);
    }

    void Sprite::setTexture(const Texture& texture)
    {
        m_texture = texture;
        if (m_texture.isValid())
        {
            // 如果纹理有效且还没有设置纹理矩形，设置为整个纹理
            if (m_textureRect.width == 0 || m_textureRect.height == 0)
            {
                // 这里需要从纹理获取实际尺寸，暂时使用1.0作为默认UV坐标
                m_textureRect = Rectf(0.0f, 0.0f, 1.0f, 1.0f);
            }
        }
    }

    void Sprite::setTextureRect(const Rectf& rect) {
        m_textureRect = rect;
    }

    const Texture& Sprite::getTexture() const {
        return m_texture;
    }

    const Rectf& Sprite::getTextureRect() const {
        return m_textureRect;
    }

    void Sprite::setPosition(const Vector2f& position) {
        m_position = position;
    }

    void Sprite::setRotation(float rotation) {
        m_rotation = rotation;
    }

    void Sprite::setScale(const Vector2f& scale) {
        m_scale = scale;
    }

    void Sprite::setOrigin(const Vector2f& origin) {
        m_origin = origin;
    }

    void Sprite::setColor(const Color& color) {
        m_color = color;
    }

    const Vector2f& Sprite::getPosition() const {
        return m_position;
    }

    float Sprite::getRotation() const {
        return m_rotation;
    }

    const Vector2f& Sprite::getScale() const {
        return m_scale;
    }

    const Vector2f& Sprite::getOrigin() const {
        return m_origin;
    }

    const Color& Sprite::getColor() const {
        return m_color;
    }

    void Sprite::move(const Vector2f& offset) {
        m_position += offset;
    }

    void Sprite::rotate(float angle) {
        m_rotation += angle;
    }

    void Sprite::scale(const Vector2f& factor) {
        m_scale.x *= factor.x;
        m_scale.y *= factor.y;
    }

    Rectf Sprite::getLocalBounds() const {
        return Rectf(0.0f, 0.0f, m_textureRect.width, m_textureRect.height);
    }

    Rectf Sprite::getGlobalBounds() const {
        // 计算全局变换后的边界
        Rectf bounds = getLocalBounds();

        // 应用缩放
        bounds.width *= m_scale.x;
        bounds.height *= m_scale.y;

        // 应用原点偏移
        bounds.left -= m_origin.x * m_scale.x;
        bounds.top -= m_origin.y * m_scale.y;

        // 应用位置
        bounds.left += m_position.x;
        bounds.top += m_position.y;

        // TODO: 如果需要，这里还可以添加旋转变换的处理

        return bounds;
    }
} // Tina
