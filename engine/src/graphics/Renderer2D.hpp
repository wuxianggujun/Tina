#pragma once

#include <bgfx/bgfx.h>
#include "math/Vector.hpp"
#include "graphics/Texture.hpp"
#include "graphics/Color.hpp"
#include <vector>

namespace Tina
{
    class Renderer2D
    {
    public:
        struct Vertex
        {
            float x, y; // 位置
            float u, v; // 纹理坐标
            uint32_t color; // 颜色 (RGBA8)

            // 基本构造函数
            Vertex(float x, float y, float u, float v, uint32_t color)
                : x(x), y(y), u(u), v(v), color(color) {}

            // Vector2f 构造函数
            Vertex(const Vector2f& pos, const Vector2f& texCoord, uint32_t color)
                : x(pos.x), y(pos.y), u(texCoord.x), v(texCoord.y), color(color) {}
        };

        Renderer2D();
        ~Renderer2D();

        void initialize(); // Declare initialize function

        // 基础图形绘制
        void drawRect(const Vector2f& position, const Vector2f& size, const Color& color);
        void drawCircle(const Vector2f& center, float radius, const Color& color, int segments = 32);
        void drawLine(const Vector2f& start, const Vector2f& end, float thickness, const Color& color);
        void drawTriangle(const Vector2f& p1, const Vector2f& p2, const Vector2f& p3, const Color& color);

        // 精灵渲染
        void drawSprite(const Vector2f& position, const Vector2f& size, const TextureHandle& texture,
                        const Vector2f& uv0 = Vector2f(0.0f, 0.0f),
                        const Vector2f& uv1 = Vector2f(1.0f, 1.0f),
                        const Color& tint = Color(1.0f, 1.0f, 1.0f, 1.0f));

        // 批处理控制
        void begin();
        void end();
        void flush();

    private:
        static const size_t MAX_VERTICES = 4096;
        static const size_t MAX_INDICES = 6144;

        void initializeBuffers();
        void submitBatch();

        std::vector<Vertex> m_vertices;
        std::vector<uint16_t> m_indices;

        bgfx::VertexBufferHandle m_vbh;
        bgfx::IndexBufferHandle m_ibh;
        bgfx::ProgramHandle m_program;
        bgfx::UniformHandle m_s_texture;

        TextureHandle m_currentTexture;
        size_t m_indexCount;
        bool m_isDrawing;

        bgfx::DynamicVertexBufferHandle m_vertexBuffer;
        bgfx::DynamicIndexBufferHandle m_indexBuffer;
        size_t m_vertexCount;
    };
}
