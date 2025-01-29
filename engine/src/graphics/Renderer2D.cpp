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
            uint32_t(MAX_VERTICES),
            PosColorTexCoordVertex::ms_layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );
        if (!bgfx::isValid(m_vbh)) {
            fmt::print("Failed to create vertex buffer\n");
            throw std::runtime_error("Failed to create vertex buffer");
        }
        fmt::print("Dynamic vertex buffer created, handle: {}\n", m_vbh.idx);

        // 创建动态索引缓冲
        m_ibh = bgfx::createDynamicIndexBuffer(
            uint32_t(MAX_INDICES),
            BGFX_BUFFER_ALLOW_RESIZE
        );
        if (!bgfx::isValid(m_ibh)) {
            fmt::print("Failed to create index buffer\n");
            throw std::runtime_error("Failed to create index buffer");
        }
        fmt::print("Dynamic index buffer created, handle: {}\n", m_ibh.idx);

        // 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("sprite.vs", "sprite.fs");
        if (!bgfx::isValid(m_program)) {
            fmt::print("Failed to load shader program\n");
            throw std::runtime_error("Failed to load shader program");
        }
        fmt::print("Successfully loaded shader program, handle: {}\n", m_program.idx);

        // 创建纹理采样器uniform
        m_s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(m_s_texColor)) {
            fmt::print("Failed to create texture sampler uniform\n");
            throw std::runtime_error("Failed to create texture sampler uniform");
        }

        // 设置视图矩形
        bgfx::setViewRect(0, 0, 0, bgfx::getStats()->width, bgfx::getStats()->height);
        
        // 设置视图清除标志
        bgfx::setViewClear(0,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x303030ff, // 背景色
            1.0f,       // 深度值
            0           // 模板值
        );
        
        fmt::print("Renderer2D initialization completed\n");
    }

    void Renderer2D::begin()
    {
        if (m_isDrawing) {
            fmt::print("Warning: begin() called while already drawing\n");
            return;
        }

        // 更新视图矩形
        uint16_t width = uint16_t(bgfx::getStats()->width);
        uint16_t height = uint16_t(bgfx::getStats()->height);
        bgfx::setViewRect(0, 0, 0, width, height);
        
        // 设置正交投影矩阵
        float ortho[16];
        bx::mtxOrtho(ortho, 
            0.0f,                // left
            float(width),        // right
            float(height),       // bottom
            0.0f,               // top
            0.0f,               // near
            1000.0f,            // far
            0.0f,               // offset
            bgfx::getCaps()->homogeneousDepth);
        
        bgfx::setViewTransform(0, NULL, ortho);
        
        // 设置视图清除标志
        bgfx::setViewClear(0,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x303030ff, // 背景色
            1.0f,       // 深度值
            0           // 模板值
        );
        
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

        fmt::print("Flushing {} vertices and {} indices\n", m_currentVertex, m_currentIndex);

        if (!bgfx::isValid(m_vbh) || !bgfx::isValid(m_ibh)) {
            fmt::print("Invalid buffer handles: vbh={}, ibh={}\n", m_vbh.idx, m_ibh.idx);
            return;
        }

        // 更新顶点和索引缓冲
        bgfx::update(m_vbh, 0, bgfx::makeRef(m_vertices, m_currentVertex * sizeof(PosColorTexCoordVertex)));
        bgfx::update(m_ibh, 0, bgfx::makeRef(m_indices, m_currentIndex * sizeof(uint16_t)));

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_MSAA
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        
        bgfx::setState(state);
        
        // 设置模型矩阵（使用单位矩阵）
        float mtx[16];
        bx::mtxIdentity(mtx);
        bgfx::setTransform(mtx);

        // 设置顶点和索引缓冲
        bgfx::setVertexBuffer(0, m_vbh, 0, m_currentVertex);
        bgfx::setIndexBuffer(m_ibh, 0, m_currentIndex);

        if (!bgfx::isValid(m_program)) {
            fmt::print("Invalid shader program handle: {}\n", m_program.idx);
            return;
        }

        fmt::print("Submitting draw call with program: {}, vbh: {}, ibh: {}\n", 
            m_program.idx, m_vbh.idx, m_ibh.idx);

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

        fmt::print("Drawing rect at ({}, {}) with size ({}, {}), color: {:#x}\n", 
            position.x, position.y, size.x, size.y, color);

        // 计算矩形的四个顶点
        uint16_t baseVertex = m_currentVertex;
        
        // 注意：Y坐标需要翻转，因为OpenGL的坐标系原点在左下角，而我们想要左上角为原点
        float y = position.y;
        float h = size.y;
        
        // 左上
        m_vertices[m_currentVertex].m_x = position.x;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = color;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 右上
        m_vertices[m_currentVertex].m_x = position.x + size.x;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = color;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 左下
        m_vertices[m_currentVertex].m_x = position.x;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = color;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 右下
        m_vertices[m_currentVertex].m_x = position.x + size.x;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = color;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 添加索引 (注意：顺序改为顺时针)
        m_indices[m_currentIndex++] = baseVertex + 0; // 左上
        m_indices[m_currentIndex++] = baseVertex + 2; // 左下
        m_indices[m_currentIndex++] = baseVertex + 1; // 右上
        m_indices[m_currentIndex++] = baseVertex + 1; // 右上
        m_indices[m_currentIndex++] = baseVertex + 2; // 左下
        m_indices[m_currentIndex++] = baseVertex + 3; // 右下
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
