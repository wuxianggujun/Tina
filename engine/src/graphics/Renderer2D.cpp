#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Tina {
    Renderer2D::Renderer2D()
        : m_vertexBuffer(BGFX_INVALID_HANDLE)
        , m_indexBuffer(BGFX_INVALID_HANDLE)
        , m_program(BGFX_INVALID_HANDLE)
        , m_vertexCount(0)
        , m_indexCount(0) {
    }

    Renderer2D::~Renderer2D() {
        if (bgfx::isValid(m_vertexBuffer)) {
            bgfx::destroy(m_vertexBuffer);
        }
        if (bgfx::isValid(m_indexBuffer)) {
            bgfx::destroy(m_indexBuffer);
        }
        if (bgfx::isValid(m_program)) {
            bgfx::destroy(m_program);
        }
    }

    void Renderer2D::initialize() {
        // 创建动态顶点缓冲区
        m_vertexBuffer = bgfx::createDynamicVertexBuffer(
            MAX_VERTICES,
            Vertex::layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );

        // 创建动态索引缓冲区
        m_indexBuffer = bgfx::createDynamicIndexBuffer(
            MAX_INDICES,
            BGFX_BUFFER_ALLOW_RESIZE
        );

        // 加载着色器程序
        m_program = BgfxUtils::loadProgram("shaders/sprite_vs", "shaders/sprite_fs");
    }

    void Renderer2D::begin() {
        m_vertexCount = 0;
        m_indexCount = 0;
    }

    void Renderer2D::end() {
        flush();
    }

    void Renderer2D::flush() {
        if (m_vertexCount == 0) {
            return;
        }

        // 更新顶点缓冲区
        const bgfx::Memory* vertexMem = bgfx::alloc(m_vertexCount * sizeof(Vertex));
        memcpy(vertexMem->data, m_vertices.data(), m_vertexCount * sizeof(Vertex));
        bgfx::update(m_vertexBuffer, 0, vertexMem);

        // 更新索引缓冲区
        const bgfx::Memory* indexMem = bgfx::alloc(m_indexCount * sizeof(uint16_t));
        memcpy(indexMem->data, m_indices.data(), m_indexCount * sizeof(uint16_t));
        bgfx::update(m_indexBuffer, 0, indexMem);

        // 设置渲染状态
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_BLEND_ALPHA);

        // 提交绘制命令
        bgfx::setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount);
        bgfx::setIndexBuffer(m_indexBuffer, 0, m_indexCount);
        bgfx::submit(0, m_program);

        // 重置计数器
        m_vertexCount = 0;
        m_indexCount = 0;
    }

    void Renderer2D::drawRect(const Vector2f& position, const Vector2f& size, const Color& color) {
        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES) {
            flush();
        }

        float x = position.x;
        float y = position.y;
        float w = size.x;
        float h = size.y;

        // 添加顶点
        m_vertices[m_vertexCount++] = {{x, y}, color, {0.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{x + w, y}, color, {1.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{x + w, y + h}, color, {1.0f, 1.0f}};
        m_vertices[m_vertexCount++] = {{x, y + h}, color, {0.0f, 1.0f}};

        // 添加索引
        uint16_t baseIndex = m_vertexCount - 4;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 1;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 3;
    }

    void Renderer2D::drawCircle(const Vector2f& center, float radius, const Color& color, int segments) {
        if (m_vertexCount + segments + 1 > MAX_VERTICES || m_indexCount + segments * 3 > MAX_INDICES) {
            flush();
        }

        // 添加中心顶点
        m_vertices[m_vertexCount++] = {{center.x, center.y}, color, {0.5f, 0.5f}};

        // 添加圆周顶点
        for (int i = 0; i < segments; ++i) {
            float angle = (float)(i * 2.0 * M_PI / segments);
            float x = center.x + radius * std::cos(angle);
            float y = center.y + radius * std::sin(angle);
            m_vertices[m_vertexCount++] = {{x, y}, color, {0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)}};
        }

        // 添加索引
        uint16_t baseIndex = m_vertexCount - segments - 1;
        for (int i = 0; i < segments; ++i) {
            m_indices[m_indexCount++] = baseIndex;
            m_indices[m_indexCount++] = baseIndex + 1 + i;
            m_indices[m_indexCount++] = baseIndex + 1 + ((i + 1) % segments);
        }
    }

    void Renderer2D::drawSprite(const Vector2f& position, const Vector2f& size, const TextureHandle& texture,
                               const Vector2f& uv0, const Vector2f& uv1, const Color& tint) {
        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES) {
            flush();
        }

        uint32_t colorValue = tint.toABGR();
        uint16_t startVertex = m_vertexCount;

        // 添加顶点
        m_vertices[m_vertexCount++] = {{position.x, position.y}, colorValue, uv0};
        m_vertices[m_vertexCount++] = {{position.x + size.x, position.y}, colorValue, uv1};
        m_vertices[m_vertexCount++] = {{position.x + size.x, position.y + size.y}, colorValue, uv1};
        m_vertices[m_vertexCount++] = {{position.x, position.y + size.y}, colorValue, uv0};

        // 添加索引
        uint16_t baseIndex = startVertex;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 1;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 3;

        // 设置纹理
        bgfx::setTexture(0, m_s_texture, texture);
    }

    void Renderer2D::drawLine(const Vector2f& start, const Vector2f& end, float thickness, const Color& color) {
        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES) {
            flush();
        }

        Vector2f direction = Vector2f(end.x - start.x, end.y - start.y);
        float length = sqrt(direction.x * direction.x + direction.y * direction.y);
        if (length > 0) {
            direction.x /= length;
            direction.y /= length;
        }
        Vector2f normal(-direction.y, direction.x);
        normal = Vector2f(normal.x * thickness * 0.5f, normal.y * thickness * 0.5f);

        uint32_t colorValue = color.toABGR();
        uint16_t startVertex = m_vertexCount;

        // 添加顶点
        m_vertices[m_vertexCount++] = {{start.x - normal.x, start.y - normal.y}, colorValue, {0.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{start.x + normal.x, start.y + normal.y}, colorValue, {1.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{end.x + normal.x, end.y + normal.y}, colorValue, {1.0f, 1.0f}};
        m_vertices[m_vertexCount++] = {{end.x - normal.x, end.y - normal.y}, colorValue, {0.0f, 1.0f}};

        // 添加索引
        uint16_t baseIndex = startVertex;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 1;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 2;
        m_indices[m_indexCount++] = baseIndex + 3;
    }

    void Renderer2D::drawTriangle(const Vector2f& p1, const Vector2f& p2, const Vector2f& p3, const Color& color) {
        if (m_vertexCount + 3 > MAX_VERTICES || m_indexCount + 3 > MAX_INDICES) {
            flush();
        }

        uint32_t colorValue = color.toABGR();
        uint16_t startVertex = m_vertexCount;

        // 添加顶点
        m_vertices[m_vertexCount++] = {{p1.x, p1.y}, colorValue, {0.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{p2.x, p2.y}, colorValue, {1.0f, 0.0f}};
        m_vertices[m_vertexCount++] = {{p3.x, p3.y}, colorValue, {0.5f, 1.0f}};

        // 添加索引
        uint16_t baseIndex = startVertex;
        m_indices[m_indexCount++] = baseIndex + 0;
        m_indices[m_indexCount++] = baseIndex + 1;
        m_indices[m_indexCount++] = baseIndex + 2;
    }
} 