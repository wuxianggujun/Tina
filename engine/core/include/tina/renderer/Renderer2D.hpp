#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>
#include <tina/log/Logger.hpp>

#include "bgfx/platform.h"

namespace Tina {

struct Vertex2D {
    float x;
    float y;
    float z;
    float u;
    float v;
    uint32_t color;

    static void init() {
        try {
            TINA_LOG_DEBUG("Initializing Vertex2D layout");
            layout
                .begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();

            if (!layout.getStride()) {
                throw std::runtime_error("Failed to initialize vertex layout");
            }

            TINA_LOG_DEBUG("Vertex2D layout initialized successfully with stride: {}", layout.getStride());
        } catch (const std::exception& e) {
            TINA_LOG_ERROR("Failed to initialize Vertex2D layout: {}", e.what());
            throw;
        }
    }

    static bgfx::VertexLayout layout;
};

class Renderer2D {
public:
    static void init(bgfx::ProgramHandle shaderProgram);
    static void shutdown();
    static bool isInitialized() { return s_Data.isInitialized; }

    static void beginScene(const glm::mat4& viewProj);
    static void endScene();

    // 基本图形绘制
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, uint32_t color);
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
    
    // 纹理绘制
    static void drawQuad(const glm::vec2& position, const glm::vec2& size, bgfx::TextureHandle texture, uint32_t color = 0xffffffff);

    // 批处理相关
    static void flush();

private:
    struct Renderer2DData {
        static const uint32_t MaxQuads = 10000;
        static const uint32_t MaxVertices = MaxQuads * 4;
        static const uint32_t MaxIndices = MaxQuads * 6;

        bgfx::ProgramHandle shader = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;

        Vertex2D* vertexBufferBase = nullptr;
        Vertex2D* vertexBufferPtr = nullptr;
        uint16_t* indices = nullptr;

        uint32_t quadCount = 0;
        bool isInitialized = false;
    };

    static Renderer2DData s_Data;
};

} // namespace Tina
