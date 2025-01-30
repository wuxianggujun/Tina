#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bx/math.h>
#include <fmt/format.h>
#include "Sprite.hpp"

namespace Tina
{
    bgfx::VertexLayout PosColorTexCoordVertex::ms_layout;

    Renderer2D::Renderer2D(uint16_t viewId)
        : m_viewId(viewId)
          , m_program(BGFX_INVALID_HANDLE)
          , m_vbh(BGFX_INVALID_HANDLE)
          , m_ibh(BGFX_INVALID_HANDLE)
          , m_s_texColor(BGFX_INVALID_HANDLE)
          , m_vertices(nullptr)
          , m_indices(nullptr)
          , m_currentVertex(0)
          , m_currentIndex(0)
          , m_currentTexture(BGFX_INVALID_HANDLE)
          , m_isDrawing(false)
          , m_camera(nullptr)
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
        fmt::print("Vertex layout initialized with stride: {}\n", PosColorTexCoordVertex::ms_layout.getStride());

        // 验证顶点布局
        if (PosColorTexCoordVertex::ms_layout.getStride() == 0)
        {
            fmt::print("Error: Invalid vertex layout stride\n");
            throw std::runtime_error("Invalid vertex layout");
        }

        // 分配批处理缓冲区
        m_vertices = new PosColorTexCoordVertex[MAX_VERTICES];
        m_indices = new uint16_t[MAX_INDICES];

        // 创建动态顶点缓冲
        m_vbh = bgfx::createDynamicVertexBuffer(
            MAX_VERTICES,
            PosColorTexCoordVertex::ms_layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );
        fmt::print("Created vertex buffer with handle: {}\n", m_vbh.idx);
        if (!bgfx::isValid(m_vbh))
        {
            fmt::print("Failed to create vertex buffer (invalid handle)\n");
            throw std::runtime_error("Failed to create vertex buffer");
        }
        fmt::print("Vertex buffer created successfully\n");

        // 创建动态索引缓冲
        m_ibh = bgfx::createDynamicIndexBuffer(
            MAX_INDICES,
            BGFX_BUFFER_ALLOW_RESIZE
        );
        fmt::print("Created index buffer with handle: {}\n", m_ibh.idx);
        if (!bgfx::isValid(m_ibh))
        {
            fmt::print("Failed to create index buffer (invalid handle)\n");
            throw std::runtime_error("Failed to create index buffer");
        }
        fmt::print("Index buffer created successfully\n");

        // 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("sprite.vs", "sprite.fs");
        if (!bgfx::isValid(m_program))
        {
            fmt::print("Failed to load shader program\n");
            throw std::runtime_error("Failed to load shader program");
        }
        fmt::print("Successfully loaded shader program, handle: {}\n", m_program.idx);

        // 创建纹理采样器uniform
        m_s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(m_s_texColor))
        {
            fmt::print("Failed to create texture sampler uniform\n");
            throw std::runtime_error("Failed to create texture sampler uniform");
        }

        fmt::print("Renderer2D initialization completed\n");
    }

    void Renderer2D::begin()
    {
        if (m_isDrawing)
        {
            fmt::print("Warning: begin() called while already drawing\n");
            return;
        }

        if (!m_camera)
        {
            fmt::print("Warning: No camera set for Renderer2D\n");
            return;
        }

        // 获取视图和投影矩阵
        const glm::mat4& view = m_camera->getViewMatrix();
        const glm::mat4& proj = m_camera->getProjectionMatrix();

        // 设置视图和投影矩阵
        bgfx::setViewTransform(m_viewId, &view[0][0], &proj[0][0]);

        // 设置视图矩形
        uint16_t width = uint16_t(bgfx::getStats()->width);
        uint16_t height = uint16_t(bgfx::getStats()->height);
        bgfx::setViewRect(m_viewId, 0, 0, width, height);

        // 设置视图清除标志
        bgfx::setViewClear(m_viewId,
                           BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0x303030ff, // 背景色
                           1.0f, // 深度值
                           0 // 模板值
        );

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(state);

        m_isDrawing = true;
        m_currentVertex = 0;
        m_currentIndex = 0;
        m_currentTexture = BGFX_INVALID_HANDLE;
    }

    void Renderer2D::end()
    {
        if (!m_isDrawing)
        {
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

        if (!bgfx::isValid(m_vbh) || !bgfx::isValid(m_ibh))
        {
            fmt::print("Invalid buffer handles: vbh={}, ibh={}\n", m_vbh.idx, m_ibh.idx);
            return;
        }

        // 更新动态缓冲区
        bgfx::update(m_vbh, 0, bgfx::makeRef(m_vertices, m_currentVertex * sizeof(PosColorTexCoordVertex)));
        bgfx::update(m_ibh, 0, bgfx::makeRef(m_indices, m_currentIndex * sizeof(uint16_t)));

        // 设置渲染状态 - 简化2D渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

        bgfx::setState(state);

        // 设置模型矩阵为单位矩阵
        float mtx[16];
        bx::mtxIdentity(mtx);
        bgfx::setTransform(mtx);

        // 如果有纹理，设置纹理
        if (m_currentTexture.isValid())
        {
            bgfx::setTexture(0, m_s_texColor, m_currentTexture.getHandle());
        }

        // 设置顶点和索引缓冲
        bgfx::setVertexBuffer(0, m_vbh, 0, m_currentVertex);
        bgfx::setIndexBuffer(m_ibh, 0, m_currentIndex);

        // 提交绘制命令
        bgfx::submit(m_viewId, m_program);

        // 重置计数器
        m_currentVertex = 0;
        m_currentIndex = 0;
    }

    void Renderer2D::drawRect(const Vector2f& position, const Vector2f& size, const Color& color)
    {
        if (!m_isDrawing)
        {
            fmt::print("Warning: drawRect() called without begin()\n");
            return;
        }

        if (checkFlush(4, 6))
        {
            flush();
        }

        fmt::print("Drawing rect at position ({}, {}), size ({}, {})\n",
                   position.x, position.y, size.x, size.y);

        uint32_t abgr = color.toABGR();

        // 计算矩形的四个顶点
        uint16_t baseVertex = m_currentVertex;

        float x = position.x;
        float y = position.y;
        float w = size.x;
        float h = size.y;

        // 左上
        m_vertices[m_currentVertex].m_x = x;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 右上
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 左下
        m_vertices[m_currentVertex].m_x = x;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 右下
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = 0.0f;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 添加索引 (顺时针顺序)
        m_indices[m_currentIndex++] = baseVertex + 0; // 左上
        m_indices[m_currentIndex++] = baseVertex + 1; // 右上
        m_indices[m_currentIndex++] = baseVertex + 2; // 左下
        m_indices[m_currentIndex++] = baseVertex + 1; // 右上
        m_indices[m_currentIndex++] = baseVertex + 3; // 右下
        m_indices[m_currentIndex++] = baseVertex + 2; // 左下
    }

    void Renderer2D::drawTexturedRect(const Vector2f& position, const Vector2f& size,
                                      const Texture& texture, const Color& color)
    {
        if (!texture.isValid())
        {
            fmt::print("Warning: Attempting to draw with invalid texture\n");
            return;
        }

        if (m_currentTexture != texture)
        {
            flush();
            m_currentTexture = texture;
        }
        drawRect(position, size, color);
    }

    void Renderer2D::drawSprite(const Sprite& sprite)
    {
        if (!sprite.getTexture().isValid())
        {
            return;
        }

        const Vector2f& position = sprite.getPosition();
        const Vector2f& scale = sprite.getScale();
        const Vector2f& origin = sprite.getOrigin();

        float rotation = sprite.getRotation();
        const Color& color = sprite.getColor();
        const Rectf& textureRect = sprite.getTextureRect();

        if (m_currentTexture != sprite.getTexture())
        {
            flush();
            m_currentTexture = sprite.getTexture();
        }

        if (!m_isDrawing)
        {
            fmt::print("Warning: drawSprite() called without begin()\n");
            return;
        }

        if (checkFlush(4, 6))
        {
            flush();
        }

        // 计算变换后的顶点位置
        float cosRotation = std::cos(rotation);
        float sinRotation = std::sin(rotation);

        Vector2f size(textureRect.width * scale.x, textureRect.height * scale.y);
        Vector2f scaledOrigin(origin.x * scale.x, origin.y * scale.y);

        // 计算四个顶点
        Vector2f vertices[4] = {
            Vector2f(-scaledOrigin.x, -scaledOrigin.y), // 左上
            Vector2f(size.x - scaledOrigin.x, -scaledOrigin.y), // 右上
            Vector2f(-scaledOrigin.x, size.y - scaledOrigin.y), // 左下
            Vector2f(size.x - scaledOrigin.x, size.y - scaledOrigin.y) // 右下
        };

        // 应用旋转和平移
        for (auto& vertex : vertices)
        {
            float x = vertex.x;
            float y = vertex.y;
            vertex.x = x * cosRotation - y * sinRotation + position.x;
            vertex.y = x * sinRotation + y * cosRotation + position.y;
        }

        // 设置顶点数据
        uint32_t abgr = color.toABGR();
        uint16_t baseVertex = m_currentVertex;

        // UV坐标
        float u1 = textureRect.left;
        float v1 = textureRect.top;
        float u2 = textureRect.left + textureRect.width;
        float v2 = textureRect.top + textureRect.height;

        // 添加顶点
        for (int i = 0; i < 4; ++i)
        {
            m_vertices[m_currentVertex].m_x = vertices[i].x;
            m_vertices[m_currentVertex].m_y = vertices[i].y;
            m_vertices[m_currentVertex].m_z = 0.0f;
            m_vertices[m_currentVertex].m_rgba = abgr;
            m_vertices[m_currentVertex].m_u = (i == 1 || i == 3) ? u2 : u1;
            m_vertices[m_currentVertex].m_v = (i == 2 || i == 3) ? v2 : v1;
            m_currentVertex++;
        }

        // 添加索引
        m_indices[m_currentIndex++] = baseVertex + 0;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 2;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 3;
        m_indices[m_currentIndex++] = baseVertex + 2;
    }

    void Renderer2D::render()
    {
        if (m_isDrawing)
        {
            fmt::print("Warning: render() called while still drawing\n");
            end();
        }
    }
}
