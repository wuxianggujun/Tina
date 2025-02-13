#pragma once

#include "RectangleComponent.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <memory>

namespace Tina {

class SpriteComponent : public RectangleComponent {
public:
    SpriteComponent() = default;
    SpriteComponent(const std::shared_ptr<Texture2D>& texture)
        : m_Texture(texture) {
        if (texture) {
            setSize({static_cast<float>(texture->getWidth()), 
                    static_cast<float>(texture->getHeight())});
        }
    }

    // 设置纹理
    void setTexture(const std::shared_ptr<Texture2D>& texture) {
        m_Texture = texture;
        if (texture) {
            setSize({static_cast<float>(texture->getWidth()), 
                    static_cast<float>(texture->getHeight())});
        }
    }

    // 获取纹理
    const std::shared_ptr<Texture2D>& getTexture() const { return m_Texture; }

    // 设置UV坐标(用于精灵图集)
    void setTextureRect(const glm::vec4& rect) { m_TextureRect = rect; }
    const glm::vec4& getTextureRect() const { return m_TextureRect; }

    // 更新顶点UV坐标
    void updateVertexBuffer(void* buffer, uint32_t size) override;

private:
    std::shared_ptr<Texture2D> m_Texture;
    glm::vec4 m_TextureRect = {0.0f, 0.0f, 1.0f, 1.0f}; // x, y, w, h in UV coordinates
};

} // namespace Tina 