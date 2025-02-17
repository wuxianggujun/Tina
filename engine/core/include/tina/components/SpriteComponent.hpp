#pragma once

#include "Renderable2DComponent.hpp"
#include "Transform2DComponent.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <memory>

#include "tina/log/Logger.hpp"

namespace Tina {

class SpriteComponent : public Renderable2DComponent {
public:
    // 顶点数据结构
    struct Vertex {
        float x, y, z;
        float u, v;
        uint32_t color;
    };

    struct InstanceData {
        glm::vec4 Transform;    // x, y, width, height
        glm::vec4 Color;       // rgba color
        glm::vec4 TextureData; // UV coordinates
        float TextureIndex;    // texture slot
        float Padding[3];      // alignment padding
    };

    SpriteComponent() = default;

    explicit SpriteComponent(const std::shared_ptr<Texture2D>& texture)
        : m_Texture(texture) {
        if (texture) {
            // 使用纹理的原始像素尺寸
            setSize({
                static_cast<float>(texture->getWidth()),
                static_cast<float>(texture->getHeight())
            });
            TINA_CORE_LOG_DEBUG("Created sprite with original size: {}x{}", 
                texture->getWidth(), texture->getHeight());
        }
    }

    // 纹理相关
    void setTexture(const std::shared_ptr<Texture2D>& texture) {
        m_Texture = texture;
        if (texture) {
            // 使用纹理的原始像素尺寸
            setSize({
                static_cast<float>(texture->getWidth()),
                static_cast<float>(texture->getHeight())
            });
        }
    }
    const std::shared_ptr<Texture2D>& getTexture() const { return m_Texture; }

    // UV坐标
    void setTextureRect(const glm::vec4& rect) { m_TextureRect = rect; }
    const glm::vec4& getTextureRect() const { return m_TextureRect; }

    // Transform相关便捷方法
    void setScale(const glm::vec2& scale) {
        if (m_Transform) {
            m_Transform->setScale(scale);
        }
    }
    
    glm::vec2 getScale() const {
        return m_Transform ? m_Transform->getScale() : glm::vec2(1.0f);
    }
    
    void setPosition(const glm::vec2& position) {
        if (m_Transform) {
            m_Transform->setPosition(position);
        }
    }
    
    glm::vec2 getPosition() const {
        return m_Transform ? m_Transform->getPosition() : glm::vec2(0.0f);
    }

    // 设置关联的Transform组件
    void setTransform(Transform2DComponent* transform) { m_Transform = transform; }
    Transform2DComponent* getTransform() const { return m_Transform; }

    // 实现基类接口
    void updateRenderData() override {
        glm::vec2 position = m_Transform ? m_Transform->getPosition() : glm::vec2(0.0f);
        glm::vec2 scale = m_Transform ? m_Transform->getScale() : glm::vec2(1.0f);
        glm::vec2 finalSize = m_Size * scale;

        m_InstanceData.Transform = glm::vec4(position.x, position.y, finalSize.x, finalSize.y);
        m_InstanceData.Color = glm::vec4(m_Color.getR(), m_Color.getG(), m_Color.getB(), m_Color.getA());
        m_InstanceData.TextureData = m_TextureRect;
        m_InstanceData.TextureIndex = m_Texture ? 0.0f : -1.0f;

        TINA_CORE_LOG_DEBUG("Updated sprite render data: pos({},{}), size({},{})",
            position.x, position.y, finalSize.x, finalSize.y);
    }

    [[nodiscard]] bool isTextured() const override { return m_Texture != nullptr; }
    
    [[nodiscard]] const void* getVertexData() const override { return nullptr; }
    [[nodiscard]] uint32_t getVertexDataSize() const override { return 0; }
    
    [[nodiscard]] const void* getInstanceData() const override { return &m_InstanceData; }
    [[nodiscard]] uint32_t getInstanceDataSize() const override { return sizeof(InstanceData); }

    // 顶点和索引缓冲更新
    void updateVertexBuffer(void* buffer, uint32_t size);
    void updateIndexBuffer(void* buffer, uint32_t size);

private:
    std::shared_ptr<Texture2D> m_Texture;
    glm::vec4 m_TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};  // UV坐标
    InstanceData m_InstanceData{};
    Transform2DComponent* m_Transform = nullptr;  // 关联的Transform组件
};

} // namespace Tina