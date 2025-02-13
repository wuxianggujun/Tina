#include "tina/components/RectangleComponent.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

bgfx::VertexLayout RectangleComponent::s_VertexLayout;

void RectangleComponent::initVertexLayout() {
    TINA_LOG_INFO("Initializing RectangleComponent vertex layout");
    
    s_VertexLayout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    if (!s_VertexLayout.getStride()) {
        TINA_LOG_ERROR("Failed to initialize vertex layout");
        throw std::runtime_error("Failed to initialize vertex layout");
    }

    TINA_LOG_INFO("Vertex layout initialized with stride: {}", s_VertexLayout.getStride());
}

const bgfx::VertexLayout& RectangleComponent::getVertexLayout() const {
    return s_VertexLayout;
}

void RectangleComponent::updateVertexBuffer(void* buffer, uint32_t size) {
    if (size < sizeof(Vertex) * 4) {
        TINA_LOG_ERROR("Buffer size too small for rectangle vertices");
        return;
    }

    Vertex* vertices = static_cast<Vertex*>(buffer);
    uint32_t packedColor = static_cast<uint32_t>(m_Color);

    // 左上
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[0].z = 0.0f;
    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[0].color = packedColor;

    // 右上
    vertices[1].x = m_Size.x;
    vertices[1].y = 0.0f;
    vertices[1].z = 0.0f;
    vertices[1].u = 1.0f;
    vertices[1].v = 0.0f;
    vertices[1].color = packedColor;

    // 右下
    vertices[2].x = m_Size.x;
    vertices[2].y = m_Size.y;
    vertices[2].z = 0.0f;
    vertices[2].u = 1.0f;
    vertices[2].v = 1.0f;
    vertices[2].color = packedColor;

    // 左下
    vertices[3].x = 0.0f;
    vertices[3].y = m_Size.y;
    vertices[3].z = 0.0f;
    vertices[3].u = 0.0f;
    vertices[3].v = 1.0f;
    vertices[3].color = packedColor;
}

void RectangleComponent::updateIndexBuffer(void* buffer, uint32_t size) {
    if (size < sizeof(uint16_t) * 6) {
        TINA_LOG_ERROR("Buffer size too small for rectangle indices");
        return;
    }

    uint16_t* indices = static_cast<uint16_t*>(buffer);
    
    // 两个三角形组成一个矩形
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 2;
    indices[4] = 3;
    indices[5] = 0;
}

} // namespace Tina 