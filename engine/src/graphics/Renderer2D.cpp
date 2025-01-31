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
        // 确保按正确顺序释放资源
        if (bgfx::isValid(m_ibh))
        {
            bgfx::destroy(m_ibh);
            m_ibh = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(m_vbh))
        {
            bgfx::destroy(m_vbh);
            m_vbh = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(m_s_texColor))
        {
            bgfx::destroy(m_s_texColor);
            m_s_texColor = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
            m_program = BGFX_INVALID_HANDLE;
        }

        delete[] m_vertices;
        m_vertices = nullptr;

        delete[] m_indices;
        m_indices = nullptr;
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

        // 设置视图矩形
        uint16_t width = uint16_t(bgfx::getStats()->width);
        uint16_t height = uint16_t(bgfx::getStats()->height);
        bgfx::setViewRect(m_viewId, 0, 0, width, height);
        fmt::print("Setting view rectangle: {}x{}\n", width, height);

        // 设置视图清除标志
        bgfx::setViewClear(m_viewId,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x303030ff,  // 深灰色背景
            1.0f,        // 深度清除值
            0           // 模板清除值
        );

        // 设置正交投影矩阵
        float orthoProj[16];
        bx::mtxOrtho(
            orthoProj,
            0.0f,                               // left
            static_cast<float>(width),          // right
            static_cast<float>(height),         // bottom
            0.0f,                               // top
            0.0f,                               // near
            100.0f,                            // far
            0.0f,                              // offset
            bgfx::getCaps()->homogeneousDepth  // 使用正确的深度范围
        );

        // 设置视图矩阵（使用单位矩阵，因为是2D渲染）
        float view[16];
        bx::mtxIdentity(view);

        // 设置视图和投影矩阵
        bgfx::setViewTransform(m_viewId, view, orthoProj);

        // 确保视图被处理
        bgfx::touch(m_viewId);

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
        
        // 确保刷新最后的批次
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
        if (m_currentVertex == 0 || m_currentIndex == 0)
        {
            return;
        }

        fmt::print("Flushing {} vertices and {} indices\n", m_currentVertex, m_currentIndex);

        // 更新顶点缓冲区
        const bgfx::Memory* vbMem = bgfx::copy(m_vertices, m_currentVertex * sizeof(PosColorTexCoordVertex));
        bgfx::update(m_vbh, 0, vbMem);

        // 更新索引缓冲区
        const bgfx::Memory* ibMem = bgfx::copy(m_indices, m_currentIndex * sizeof(uint16_t));
        bgfx::update(m_ibh, 0, ibMem);

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LEQUAL  // 使用LEQUAL而不是LESS
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
            | BGFX_STATE_MSAA;

        bgfx::setState(state);

        // 如果有纹理，设置纹理
        if (bgfx::isValid(m_currentTexture))
        {
            fmt::print("Setting texture with handle: {} for drawing\n", m_currentTexture.idx);
            bgfx::setTexture(0, m_s_texColor, m_currentTexture);
        }

        // 设置变换矩阵 - 使用单位矩阵作为模型矩阵
        float modelMat[16];
        bx::mtxIdentity(modelMat);
        bgfx::setTransform(modelMat);

        // 设置顶点和索引缓冲
        bgfx::setVertexBuffer(0, m_vbh, 0, m_currentVertex);
        bgfx::setIndexBuffer(m_ibh, 0, m_currentIndex);

        // 提交绘制命令
        fmt::print("Submitting draw call with program handle: {}, texture: {}\n", 
            m_program.idx, 
            bgfx::isValid(m_currentTexture) ? "yes" : "no");
        bgfx::submit(m_viewId, m_program);

        // 重置计数器
        m_currentVertex = 0;
        m_currentIndex = 0;
    }

    void Renderer2D::drawRect(const Vector2f& position, const Vector2f& size, const Color& color, float depth)
    {
        if (!m_isDrawing)
        {
            fmt::print("Warning: drawRect() called without begin()\n");
            return;
        }

        // 只有当当前批次使用了不同的纹理时才需要刷新
        if (bgfx::isValid(m_currentTexture))
        {
            // 如果当前批次使用了纹理，而这次要绘制无纹理矩形
            flush();
            m_currentTexture = BGFX_INVALID_HANDLE;
        }

        // 检查是否需要刷新批处理
        if (checkFlush(4, 6))
        {
            flush();
        }

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
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;  // 这些UV坐标对于纯色矩形来说不需要
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 右上
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;  // 这些UV坐标对于纯色矩形来说不需要
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 左下
        m_vertices[m_currentVertex].m_x = x;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;  // 这些UV坐标对于纯色矩形来说不需要
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 右下
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;  // 这些UV坐标对于纯色矩形来说不需要
        m_vertices[m_currentVertex].m_v = 1.0f;
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
                                      const Texture& texture, const Color& color)
    {
        if (!texture.isValid())
        {
            fmt::print("Warning: Attempting to draw with invalid texture\n");
            return;
        }

        if (m_currentTexture.idx != texture.getIdx())
        {
            flush();
            m_currentTexture = texture.getHandle();
        }
        drawRect(position, size, color);
    }

    void Renderer2D::drawSprite(const Vector2f& position, const Vector2f& size, const Color& color, const Texture& texture, float depth)
    {
        if (!m_isDrawing)
        {
            fmt::print("Warning: drawSprite() called without begin()\n");
            return;
        }

        // 检查纹理是否有效
        if (!texture.isValid())
        {
            fmt::print("Warning: Invalid texture in drawSprite\n");
            return;
        }

        // 如果纹理发生变化，需要刷新批次
        TextureHandle newTexture = texture.getHandle();
        if (m_currentTexture.idx != newTexture.idx)
        {
            if (m_currentVertex > 0)
            {
                flush();
            }
            m_currentTexture = newTexture;
        }

        // 检查是否需要刷新批处理
        if (checkFlush(4, 6))
        {
            flush();
        }

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
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 右上
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 0.0f;
        m_currentVertex++;

        // 左下
        m_vertices[m_currentVertex].m_x = x;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 0.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 右下
        m_vertices[m_currentVertex].m_x = x + w;
        m_vertices[m_currentVertex].m_y = y + h;
        m_vertices[m_currentVertex].m_z = depth;
        m_vertices[m_currentVertex].m_rgba = abgr;
        m_vertices[m_currentVertex].m_u = 1.0f;
        m_vertices[m_currentVertex].m_v = 1.0f;
        m_currentVertex++;

        // 添加索引
        m_indices[m_currentIndex++] = baseVertex + 0;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 2;
        m_indices[m_currentIndex++] = baseVertex + 1;
        m_indices[m_currentIndex++] = baseVertex + 3;
        m_indices[m_currentIndex++] = baseVertex + 2;
    }

    void Renderer2D::drawSprite(const Sprite& sprite)
    {
        if (!sprite.getTexture().isValid())
        {
            fmt::print("Warning: Invalid texture in sprite\n");
            return;
        }

        if (!m_isDrawing)
        {
            fmt::print("Warning: drawSprite() called without begin()\n");
            return;
        }

        // 检查是否需要切换纹理
        TextureHandle newTexture = sprite.getTexture().getHandle();
        fmt::print("Drawing sprite with texture handle: {}, current texture handle: {}\n", 
                  newTexture.idx, m_currentTexture.idx);
                  
        if (m_currentTexture.idx != newTexture.idx)
        {
            fmt::print("Switching texture from {} to {}\n", m_currentTexture.idx, newTexture.idx);
            flush(); // 确保在切换纹理前刷新当前批次
            m_currentTexture = newTexture;
        }

        // 检查缓冲区是否需要刷新
        if (checkFlush(4, 6))
        {
            flush();
        }

        const Vector2f& position = sprite.getPosition();
        const Vector2f& size = sprite.getSize();
        const Vector2f& origin = sprite.getOrigin();
        const Rectf& textureRect = sprite.getTextureRect();
        const Color& color = sprite.getColor();

        // 如果精灵不可见，直接返回
        if (color.a == 0)
        {
            return;
        }

        float radians = bx::toRad(sprite.getRotation());
        float cosRotation = std::cos(radians);
        float sinRotation = std::sin(radians);

        // 计算顶点位置
        Vector2f vertices[4] = {
            Vector2f(-origin.x, -origin.y),                // 左上
            Vector2f(size.x - origin.x, -origin.y),       // 右上
            Vector2f(-origin.x, size.y - origin.y),       // 左下
            Vector2f(size.x - origin.x, size.y - origin.y) // 右下
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

        // 设置UV坐标 - 使用textureRect的尺寸
        float u1 = textureRect.left;
        float v1 = textureRect.top;
        float u2 = textureRect.left + textureRect.width;
        float v2 = textureRect.top + textureRect.height;

        fmt::print("Drawing sprite with texture handle: {}, UV: ({}, {}) -> ({}, {})\n", 
                  m_currentTexture.idx, u1, v1, u2, v2);

        // 添加顶点 - 确保UV坐标正确对应纹理区域
        for (int i = 0; i < 4; ++i)
        {
            m_vertices[m_currentVertex].m_x = vertices[i].x;
            m_vertices[m_currentVertex].m_y = vertices[i].y;
            m_vertices[m_currentVertex].m_z = 0.0f;  // 使用默认深度值
            m_vertices[m_currentVertex].m_rgba = abgr;
            
            // 左上角顶点
            if (i == 0) {
                m_vertices[m_currentVertex].m_u = u1;
                m_vertices[m_currentVertex].m_v = v1;
            }
            // 右上角顶点
            else if (i == 1) {
                m_vertices[m_currentVertex].m_u = u2;
                m_vertices[m_currentVertex].m_v = v1;
            }
            // 左下角顶点
            else if (i == 2) {
                m_vertices[m_currentVertex].m_u = u1;
                m_vertices[m_currentVertex].m_v = v2;
            }
            // 右下角顶点
            else {
                m_vertices[m_currentVertex].m_u = u2;
                m_vertices[m_currentVertex].m_v = v2;
            }
            
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
