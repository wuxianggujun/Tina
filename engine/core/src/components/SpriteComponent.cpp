#include "tina/components/SpriteComponent.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

void SpriteComponent::updateVertexBuffer(void* buffer, uint32_t size) {
    if (size < sizeof(Vertex) * 4) {
        TINA_LOG_ERROR("Buffer size too small for sprite vertices");
        return;
    }

    Vertex* vertices = static_cast<Vertex*>(buffer);
    uint32_t packedColor = static_cast<uint32_t>(m_Color);
    const glm::vec2& vec2 = getSize();

    // Calculate UV coordinates based on texture rect
    float u1 = m_TextureRect.x;
    float v1 = m_TextureRect.y;
    float u2 = u1 + m_TextureRect.z;
    float v2 = v1 + m_TextureRect.w;

    // 左上
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[0].z = 0.0f;
    vertices[0].u = u1;
    vertices[0].v = v1;
    vertices[0].color = packedColor;

    // 右上
    vertices[1].x = vec2.x;
    vertices[1].y = 0.0f;
    vertices[1].z = 0.0f;
    vertices[1].u = u2;
    vertices[1].v = v1;
    vertices[1].color = packedColor;

    // 右下
    vertices[2].x = vec2.x;
    vertices[2].y = vec2.y;
    vertices[2].z = 0.0f;
    vertices[2].u = u2;
    vertices[2].v = v2;
    vertices[2].color = packedColor;

    // 左下
    vertices[3].x = 0.0f;
    vertices[3].y = vec2.y;
    vertices[3].z = 0.0f;
    vertices[3].u = u1;
    vertices[3].v = v2;
    vertices[3].color = packedColor;
}

} // namespace Tina 