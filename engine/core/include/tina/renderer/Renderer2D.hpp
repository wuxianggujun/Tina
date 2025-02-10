#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Tina {

struct Vertex2D {
    float x;
    float y;
    float u;
    float v;
    uint32_t color;

    static void init() {
        layout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }

    static bgfx::VertexLayout layout;
};

class Renderer2D {
public:
    static void init();
    static void shutdown();

    static void beginScene(const glm::mat4& viewProj);
    static void endScene();

    // 基本图形绘制
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, uint32_t color);
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
    
    // 纹理绘制
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, bgfx::TextureHandle texture, uint32_t color = 0xffffffff);

private:
    static void flush();

    struct Renderer2DData {
        static const uint32_t MaxQuads = 10000;
        static const uint32_t MaxVertices = MaxQuads * 4;
        static const uint32_t MaxIndices = MaxQuads * 6;

        bgfx::DynamicVertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle shader = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_texture = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_viewProj = BGFX_INVALID_HANDLE;

        uint32_t quadCount = 0;
        Vertex2D* vertexBufferBase = nullptr;
        Vertex2D* vertexBufferPtr = nullptr;

        glm::mat4 viewProjection = glm::mat4(1.0f);
    };

    static Renderer2DData s_Data;
};

} // namespace Tina
