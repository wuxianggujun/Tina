#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "tina/core/Engine.hpp"

namespace Tina {

bgfx::VertexLayout Vertex2D::layout;
Renderer2D::Renderer2DData Renderer2D::s_Data;

void Renderer2D::init() {
    // 初始化顶点布局
    Vertex2D::init();

    // 创建顶点缓冲和索引缓冲
    s_Data.vertexBufferBase = new Vertex2D[s_Data.MaxVertices];
    
    uint16_t* indices = new uint16_t[s_Data.MaxIndices];
    uint16_t offset = 0;
    for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6) {
        indices[i + 0] = offset + 0;
        indices[i + 1] = offset + 1;
        indices[i + 2] = offset + 2;
        indices[i + 3] = offset + 2;
        indices[i + 4] = offset + 3;
        indices[i + 5] = offset + 0;
        offset += 4;
    }

    s_Data.indexBuffer = bgfx::createIndexBuffer(
        bgfx::makeRef(indices, sizeof(uint16_t) * s_Data.MaxIndices)
    );

    s_Data.vertexBuffer = bgfx::createDynamicVertexBuffer(
        s_Data.MaxVertices,
        Vertex2D::layout,
        BGFX_BUFFER_ALLOW_RESIZE
    );

    // 使用 ShaderManager 加载着色器
    try {
        s_Data.shader = ShaderManager::getInstance().createProgram("2d");
    } catch (const std::exception& e) {
        // 处理着色器加载错误
        throw;
    }

    // 创建 uniform 句柄
    s_Data.s_texture = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);
    s_Data.u_viewProj = bgfx::createUniform("u_viewProj", bgfx::UniformType::Mat4);

    s_Data.vertexBufferPtr = s_Data.vertexBufferBase;
    s_Data.quadCount = 0;
}

void Renderer2D::shutdown() {
    delete[] s_Data.vertexBufferBase;
    bgfx::destroy(s_Data.indexBuffer);
    bgfx::destroy(s_Data.vertexBuffer);
    bgfx::destroy(s_Data.s_texture);
    bgfx::destroy(s_Data.u_viewProj);
    
    // ShaderManager 会在其 shutdown 中处理着色器的销毁
    ShaderManager::getInstance().shutdown();
}

void Renderer2D::beginScene(const glm::mat4& viewProj) {
    s_Data.viewProjection = viewProj;
    bgfx::setUniform(s_Data.u_viewProj, glm::value_ptr(viewProj));
}

void Renderer2D::endScene() {
    flush();
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, uint32_t color) {
    if (s_Data.quadCount >= s_Data.MaxQuads) {
        flush();
    }

    float x = position.x;
    float y = position.y;
    float width = size.x;
    float height = size.y;

    s_Data.vertexBufferPtr->x = x;
    s_Data.vertexBufferPtr->y = y;
    s_Data.vertexBufferPtr->u = 0.0f;
    s_Data.vertexBufferPtr->v = 0.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x + width;
    s_Data.vertexBufferPtr->y = y;
    s_Data.vertexBufferPtr->u = 1.0f;
    s_Data.vertexBufferPtr->v = 0.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x + width;
    s_Data.vertexBufferPtr->y = y + height;
    s_Data.vertexBufferPtr->u = 1.0f;
    s_Data.vertexBufferPtr->v = 1.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x;
    s_Data.vertexBufferPtr->y = y + height;
    s_Data.vertexBufferPtr->u = 0.0f;
    s_Data.vertexBufferPtr->v = 1.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.quadCount++;
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
    uint32_t packed = ((uint8_t)(color.a * 255.0f) << 24) |
                     ((uint8_t)(color.b * 255.0f) << 16) |
                     ((uint8_t)(color.g * 255.0f) << 8) |
                     ((uint8_t)(color.r * 255.0f));
    drawQuad(position, size, packed);
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, bgfx::TextureHandle texture, uint32_t color) {
    drawQuad(position, size, color);
    // TODO: 实现纹理渲染
    bgfx::setTexture(0, s_Data.s_texture, texture);
}

void Renderer2D::flush() {
    if (s_Data.quadCount == 0) {
        return;
    }

    uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.vertexBufferPtr - (uint8_t*)s_Data.vertexBufferBase);
    bgfx::update(s_Data.vertexBuffer, 0, bgfx::makeRef(s_Data.vertexBufferBase, dataSize));

    bgfx::setVertexBuffer(0, s_Data.vertexBuffer);
    bgfx::setIndexBuffer(s_Data.indexBuffer, 0, s_Data.quadCount * 6);

    uint64_t state = BGFX_STATE_WRITE_RGB | 
                     BGFX_STATE_WRITE_A | 
                     BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    
    bgfx::setState(state);
    bgfx::submit(0, s_Data.shader);

    s_Data.quadCount = 0;
    s_Data.vertexBufferPtr = s_Data.vertexBufferBase;
}

}
