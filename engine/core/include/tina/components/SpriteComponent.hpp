#pragma once

#include "Renderable2DComponent.hpp"
#include "Transform2DComponent.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <memory>

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
        glm::vec4 Transform;    // x, y, scale_x, scale_y
        glm::vec4 Color;       // rgba color
        glm::vec4 TextureData; // UV coordinates
        float TextureIndex;    // texture slot
        float Padding[3];      // alignment padding
    };

    SpriteComponent() = default;
    explicit SpriteComponent(const std::shared_ptr<Texture2D>& texture) 
        : m_Texture(texture) {
        if (texture) {
            setSize({
                static_cast<float>(texture->getWidth()),
                static_cast<float>(texture->getHeight())
            });
        }
    }

    // 纹理相关
    void setTexture(const std::shared_ptr<Texture2D>& texture) {
        m_Texture = texture;
        if (texture) {
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

    // 缩放
    void setScale(const glm::vec2& scale) { m_Scale = scale; }
    const glm::vec2& getScale() const { return m_Scale; }

    // 设置关联的Transform组件
    void setTransform(Transform2DComponent* transform) { m_Transform = transform; }

    // 实现基类接口
    void updateRenderData() override {
        // 使用Transform组件的position
        glm::vec2 position = m_Transform ? m_Transform->getPosition() : glm::vec2(0.0f);
        m_InstanceData.Transform = glm::vec4(position.x, position.y, m_Size.x * m_Scale.x, m_Size.y * m_Scale.y);
        m_InstanceData.Color = glm::vec4(m_Color.getR(), m_Color.getG(), m_Color.getB(), m_Color.getA());
        m_InstanceData.TextureData = m_TextureRect;
        m_InstanceData.TextureIndex = m_Texture ? 0.0f : -1.0f;
    }

    bool isTextured() const override { return m_Texture != nullptr; }
    
    const void* getVertexData() const override { return nullptr; }
    uint32_t getVertexDataSize() const override { return 0; }
    
    const void* getInstanceData() const override { return &m_InstanceData; }
    uint32_t getInstanceDataSize() const override { return sizeof(InstanceData); }

    // 顶点和索引缓冲更新
    void updateVertexBuffer(void* buffer, uint32_t size);
    void updateIndexBuffer(void* buffer, uint32_t size);

private:
    std::shared_ptr<Texture2D> m_Texture;
    glm::vec4 m_TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec2 m_Scale = {1.0f, 1.0f};
    InstanceData m_InstanceData;
    Transform2DComponent* m_Transform = nullptr;  // 关联的Transform组件
};

} // namespace Tina 