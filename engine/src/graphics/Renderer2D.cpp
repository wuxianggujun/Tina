#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <fmt/format.h>

namespace Tina
{
    bgfx::VertexLayout PosColorTexCoordVertex::ms_layout;

    Renderer2D::Renderer2D()
        : m_program(BGFX_INVALID_HANDLE)
        , m_vbh(BGFX_INVALID_HANDLE)
        , m_ibh(BGFX_INVALID_HANDLE)
        , m_s_texColor(BGFX_INVALID_HANDLE)
        , m_vertices(nullptr)
        , m_indices(nullptr)
        , m_currentVertex(0)
        , m_currentIndex(0)
        , m_currentTexture(BGFX_INVALID_HANDLE)
        , m_isDrawing(false)
    {
    }

    Renderer2D::~Renderer2D()
    {
        if (bgfx::isValid(m_ibh))
            bgfx::destroy(m_ibh);
        if (bgfx::isValid(m_vbh))
            bgfx::destroy(m_vbh);
        if (bgfx::isValid(m_program))
            bgfx::destroy(m_program);
        if (bgfx::isValid(m_s_texColor))
            bgfx::destroy(m_s_texColor);

        delete[] m_vertices;
        delete[] m_indices;
    }

    void Renderer2D::initialize()
    {
        fmt::print("Initializing Renderer2D...\n");
        
        // 初始化顶点布局
        PosColorTexCoordVertex::init();
        fmt::print("Vertex layout initialized\n");

        // 分配批处理缓冲区
        m_vertices = new PosColorTexCoordVertex[MAX_VERTICES];
        m_indices = new uint16_t[MAX_INDICES];
        
        // 创建动态顶点缓冲
        m_vbh = bgfx::createDynamicVertexBuffer(
            MAX_VERTICES,
            PosColorTexCoordVertex::ms_layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );
        fmt::print("Dynamic vertex buffer created, handle: {}\n", m_vbh.idx);

        // 创建动态索引缓冲
        m_ibh = bgfx::createDynamicIndexBuffer(
            MAX_INDICES,
            BGFX_BUFFER_ALLOW_RESIZE
        );
        fmt::print("Dynamic index buffer created, handle: {}\n", m_ibh.idx);

        // 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("sprite.vs", "sprite.fs");
        if (!bgfx::isValid(m_program)) {
            fmt::print("Failed to load shader program\n");
            throw std::runtime_error("Failed to load shader program");
        }

        // 创建纹理采样器uniform
        m_s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        
        fmt::print("Renderer2D initialization completed\n");
    }

    void Renderer2D::begin()
    {
        if (m_isDrawing) {
            fmt::print("Warning: begin() called while already drawing\n");
            return;
        }
        m_isDrawing = true;
        m_currentVertex = 0;
        m_currentIndex = 0;
        m_currentTexture = BGFX_INVALID_HANDLE;
    }

    void Renderer2D::end()
    {
        if (!m_isDrawing) {
            fmt::print("Warning: end() called while not drawing\n");
            return;
        }
        flush();
        m_isDrawing = false;
    }

    bool Renderer2D::checkFlush(uint16_t vertexCount, uint16_t indexCount)
    {
        return (m_currentVertex + vertexCount > MAX_VERTICES) || 
               (m_currentIndex + indexCount > MAX_INDICES);
    }

    void Renderer2D::flush()
    {
        if (m_currentVertex == 0)
            return;

        // 更新顶点和索引缓冲
        bgfx::update(m_vbh, 0, bgfx::makeRef(m_vertices, m_currentVertex * sizeof(PosColorTexCoordVertex)));
        bgfx::update(m_ibh, 0, bgfx::makeRef(m_indices, m_currentIndex * sizeof(uint16_t)));

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);

        // 设置变换矩阵（使用单位矩阵）
        float mtx[16];
        bx::mtxIdentity(mtx);
        bgfx::setTransform(mtx);

        // 设置顶点和索引缓冲
        bgfx::setVertexBuffer(0, m_vbh, 0, m_currentVertex);
        bgfx::setIndexBuffer(m_ibh, 0, m_currentIndex);

        // 如果有纹理，设置纹理
        if (bgfx::isValid(m_currentTexture)) {
            bgfx::setTexture(0, m_s_texColor, m_currentTexture);
        }

        // 提交绘制命令
        bgfx::submit(0, m_program);

        // 重置计数器
        m_currentVertex = 0;
        m_currentIndex = 0;
    }

    void Renderer2D::drawRect(const Vector2f& position, const Vector2f& size, uint32_t color)
    {
        if (!m_isDrawing) {
            fmt::print("Warning: drawRect() called without begin()\n");
            return;
        }

        if (checkFlush(4, 6)) {
            flush();
        }

        // 计算矩形的四个顶点
        uint16_t baseVertex = m_currentVertex;
        
        // 左上
        m_vertices[m_currentVertex].x = position.x;
        m_vertices[m_currentVertex].y = position.y;
        m_vertices[m_currentVertex].z = 0.0f;
        m_vertices[m_currentVertex].color = color;
        m_vertices[m_currentVertex].u = 0.0f;
        m_vertices[m_currentVertex].v = 0.0f;
        m_currentVertex++;

        // 右上
        m_vertices[m_currentVertex].x = position.x + size.x;
        m_vertices[m_currentVertex].y = position.y;
        m_vertices[m_currentVertex].z = 0.0f;
        m_vertices[m_currentVertex].color = color;
        m_vertices[m_currentVertex].u = 1.0f;
        m_vertices[m_currentVertex].v = 0.0f;
        m_currentVertex++;

        // 左下
        m_vertices[m_currentVertex].x = position.x;
        m_vertices[m_currentVertex].y = position.y + size.y;
        m_vertices[m_currentVertex].z = 0.0f;
        m_vertices[m_currentVertex].color = color;
        m_vertices[m_currentVertex].u = 0.0f;
        m_vertices[m_currentVertex].v = 1.0f;
        m_currentVertex++;

        // 右下
        m_vertices[m_currentVertex].x = position.x + size.x;
        m_vertices[m_currentVertex].y = position.y + size.y;
        m_vertices[m_currentVertex].z = 0.0f;
        m_vertices[m_currentVertex].color = color;
        m_vertices[m_currentVertex].u = 1.0f;
        m_vertices[m_currentVertex].v = 1.0f;
        m_currentVertex++;

        // 添加索引
        m_indices[m_currentIndex++] = baseVertex + 0;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 2;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 3;
        m_indices[m_currentIndex++] = baseVertex + 2;
    }

    void Renderer2D::drawTexturedRect(const Vector2f& position, const Vector2f& size, 
                                    bgfx::TextureHandle texture, uint32_t color)
    {
        if (m_currentTexture.idx != texture.idx) {
            flush();
            m_currentTexture = texture;
        }
        drawRect(position, size, color);
    }

    void Renderer2D::render()
    {
        if (m_isDrawing) {
            fmt::print("Warning: render() called while still drawing\n");
            end();
        }
    }
}
