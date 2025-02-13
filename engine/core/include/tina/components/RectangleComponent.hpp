#pragma once

#include "RenderableComponent.hpp"
#include <glm/glm.hpp>

namespace Tina {

class RectangleComponent : public RenderableComponent {
public:
    RectangleComponent() = default;
    RectangleComponent(const glm::vec2& size)
        : m_Size(size) {}

    // Size
    void setSize(const glm::vec2& size) { m_Size = size; }
    const glm::vec2& getSize() const { return m_Size; }

    // 实现RenderableComponent接口
    uint32_t getVertexCount() const override { return 4; }
    uint32_t getIndexCount() const override { return 6; }
    
    const bgfx::VertexLayout& getVertexLayout() const override;
    
    void updateVertexBuffer(void* buffer, uint32_t size) override;
    void updateIndexBuffer(void* buffer, uint32_t size) override;

    // 获取顶点数据
    struct Vertex {
        float x, y, z;
        float u, v;
        uint32_t color;
    };

    static void initVertexLayout();
    static bgfx::VertexLayout s_VertexLayout;

private:
    glm::vec2 m_Size = {1.0f, 1.0f};
};

} // namespace Tina 