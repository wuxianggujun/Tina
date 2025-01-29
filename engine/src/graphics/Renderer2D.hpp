#pragma once

#include <bgfx/bgfx.h>

#include "Color.hpp"
#include "math/Vector.hpp"

namespace Tina
{
    // 顶点结构体，包含位置、颜色和纹理坐标
    struct PosColorTexCoordVertex
    {
        float m_x;
        float m_y;
        float m_z;
        uint32_t m_rgba;
        float m_u;
        float m_v;

        static void init()
        {
            ms_layout
                .begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
        }

        static bgfx::VertexLayout ms_layout;
    };

    class Renderer2D
    {
    public:
        explicit Renderer2D(uint16_t viewId = 0);
        ~Renderer2D();

        // 初始化渲染器
        void initialize();

        // 设置视图ID
        void setViewId(uint16_t viewId) { m_viewId = viewId; }

        // 设置视图投影矩阵
        void setViewProjection(float width, float height);

        // 绘制纯色矩形
        void drawRect(const Vector2f& position, const Vector2f& size, const Color& color);

        // 绘制纹理矩形
        void drawTexturedRect(const Vector2f& position, const Vector2f& size, bgfx::TextureHandle texture, uint32_t color = 0xffffffff);

        // 开始和结束批处理
        void begin();
        void end();

        // 提交渲染
        void render();

    private:
        // 刷新批处理缓冲区
        void flush();

        // 检查是否需要刷新批处理
        bool checkFlush(uint16_t vertexCount, uint16_t indexCount);

        uint16_t m_viewId;  // 视图ID
        bgfx::ProgramHandle m_program;
        bgfx::DynamicVertexBufferHandle m_vbh;
        bgfx::DynamicIndexBufferHandle m_ibh;
        bgfx::UniformHandle m_s_texColor; // 纹理采样器uniform

        // 批处理相关
        static const uint16_t MAX_VERTICES = 1024;
        static const uint16_t MAX_INDICES = 2048;

        PosColorTexCoordVertex* m_vertices;
        uint16_t* m_indices;
        uint16_t m_currentVertex;
        uint16_t m_currentIndex;

        bgfx::TextureHandle m_currentTexture;
        bool m_isDrawing;
    };
}