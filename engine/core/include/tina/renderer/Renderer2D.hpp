#pragma once

#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>
#include <tina/log/Logger.hpp>
#include <tina/renderer/Color.hpp>
#include "bgfx/platform.h"

namespace Tina
{
    class Color;

    struct Vertex2D
    {
        float x, y, z;
        float u, v;
        uint32_t color;

        static void init();

        static bgfx::VertexLayout layout;
    };

    class Renderer2D
    {
    public:
        static void init(bgfx::ProgramHandle shaderProgram);
        static void shutdown();
        static bool isInitialized() { return s_Data.isInitialized; }

        static void beginScene(const glm::mat4& viewProj);
        static void endScene();

        // 矩形绘制
        static void drawRect(const glm::vec2& position, const glm::vec2& size, const Color& color);
        static void drawTexturedRect(const glm::vec2& position, const glm::vec2& size,
                                     bgfx::TextureHandle texture, const Color& color = Color::White);
        static void drawRotatedRect(const glm::vec2& position, const glm::vec2& size,
                                    float rotation, const Color& color);

        // 圆形绘制
        static void drawCircle(const glm::vec2& center, float radius, const Color& color);
        static void drawCircleOutline(const glm::vec2& center, float radius,
                                      float thickness, const Color& color);

        // 线段绘制
        static void drawLine(const glm::vec2& start, const glm::vec2& end,
                             float thickness, const Color& color);

        // 三角形绘制
        static void drawTriangle(const glm::vec2& p1, const glm::vec2& p2,
                                 const glm::vec2& p3, const Color& color);

        // 批处理相关
        static void flush();

    private:
        struct Renderer2DData
        {
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
