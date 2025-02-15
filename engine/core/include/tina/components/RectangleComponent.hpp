#pragma once

#include "Renderable2DComponent.hpp"
#include "Transform2DComponent.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include <glm/glm.hpp>

namespace Tina {

class RectangleComponent : public Renderable2DComponent {
public:
    RectangleComponent() = default;
    
    // Transform相关
    void setTransform(Transform2DComponent* transform) { m_Transform = transform; }
    Transform2DComponent* getTransform() const { return m_Transform; }

    // 实现Renderable2DComponent接口
    void updateRenderData() override {
        glm::vec2 position = m_Transform ? m_Transform->getPosition() : glm::vec2(0.0f);
        glm::vec2 scale = m_Transform ? m_Transform->getScale() : glm::vec2(1.0f);
        glm::vec2 finalSize = m_Size * scale;

        m_InstanceData.Transform = glm::vec4(position.x, position.y, finalSize.x, finalSize.y);
        m_InstanceData.Color = glm::vec4(m_Color.getR(), m_Color.getG(), m_Color.getB(), m_Color.getA());
        m_InstanceData.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        m_InstanceData.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);  // 非纹理quad
    }

    bool isTextured() const override { return false; }
    
    const void* getVertexData() const override { return nullptr; }
    uint32_t getVertexDataSize() const override { return 0; }
    
    const void* getInstanceData() const override { return &m_InstanceData; }
    uint32_t getInstanceDataSize() const override { return sizeof(BatchRenderer2D::InstanceData); }

private:
    Transform2DComponent* m_Transform = nullptr;
    BatchRenderer2D::InstanceData m_InstanceData{};
};

} // namespace Tina 