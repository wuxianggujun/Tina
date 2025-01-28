#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Tina
{
    Renderer2D::Renderer2D()
        : m_vbh(BGFX_INVALID_HANDLE), m_ibh(BGFX_INVALID_HANDLE), m_program(BGFX_INVALID_HANDLE)
          , m_s_texture(BGFX_INVALID_HANDLE)
          , m_currentTexture(BGFX_INVALID_HANDLE)
          , m_indexCount(0), m_isDrawing(false), m_vertexBuffer(BGFX_INVALID_HANDLE)
          , m_indexBuffer(BGFX_INVALID_HANDLE), m_vertexCount(0)
    {
    }

    Renderer2D::~Renderer2D()
    {
        if (bgfx::isValid(m_vertexBuffer))
        {
            bgfx::destroy(m_vertexBuffer);
        }
        if (bgfx::isValid(m_indexBuffer))
        {
            bgfx::destroy(m_indexBuffer);
        }
        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }
        if (bgfx::isValid(m_s_texture))
        {
            bgfx::destroy(m_s_texture);
        }
    }

    void Renderer2D::initialize()
    {
        // 首先创建顶点布局
        bgfx::VertexLayout layout;
        layout.begin()
              .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
              .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
              .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
              .end();

        // 创建动态顶点缓冲区
        m_vertexBuffer = bgfx::createDynamicVertexBuffer(
            MAX_VERTICES,
            layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );

        if (!bgfx::isValid(m_vertexBuffer)) {
            fmt::print("Failed to create vertex buffer\n");
            throw std::runtime_error("Failed to create vertex buffer");
        }

        // 创建动态索引缓冲区
        m_indexBuffer = bgfx::createDynamicIndexBuffer(
            MAX_INDICES,
            BGFX_BUFFER_ALLOW_RESIZE
        );

        if (!bgfx::isValid(m_indexBuffer)) {
            fmt::print("Failed to create index buffer\n");
            throw std::runtime_error("Failed to create index buffer");
        }

        // 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("sprite.vs", "sprite.fs");
        
        if (!bgfx::isValid(m_program)) {
            fmt::print("Failed to load shader program\n");
            throw std::runtime_error("Failed to load shader program");
        }
        fmt::print("Successfully loaded shader program, handle: {}\n", m_program.idx);

        // 等待一帧确保着色器程序完全加载
        bgfx::frame();
        bgfx::frame(); // 等待两帧以确保完全加载
        
        // 创建 MVP uniform
        fmt::print("Creating MVP uniform...\n");
        bgfx::UniformHandle mvp = bgfx::createUniform("u_modelViewProj", bgfx::UniformType::Mat4);
        if (!bgfx::isValid(mvp)) {
            fmt::print("Failed to create MVP uniform. This might happen if the uniform is not used in the shader.\n");
            fmt::print("Shader program handle: {}, is valid: {}\n", m_program.idx, bgfx::isValid(m_program));
            throw std::runtime_error("Failed to create MVP uniform");
        }
        fmt::print("Successfully created MVP uniform\n");
        
        // 初始化 ShaderUniform
        m_u_modelViewProj = ShaderUniform();
        m_u_modelViewProj.setHandle(mvp);

        // 创建采样器 uniform
        fmt::print("Creating sampler uniform...\n");
        m_s_texture = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(m_s_texture)) {
            fmt::print("Failed to create sampler uniform\n");
            throw std::runtime_error("Failed to create sampler uniform");
        }
        fmt::print("Successfully created sampler uniform, handle: {}\n", m_s_texture.idx);

        // 确保所有资源都已正确初始化
        if (!bgfx::isValid(m_program) || !bgfx::isValid(m_s_texture) || !m_u_modelViewProj.isValid()) {
            fmt::print("Resource validation failed:\n");
            fmt::print("  Program valid: {}\n", bgfx::isValid(m_program));
            fmt::print("  Sampler valid: {}\n", bgfx::isValid(m_s_texture));
            fmt::print("  MVP valid: {}\n", m_u_modelViewProj.isValid());
            throw std::runtime_error("Resource validation failed");
        }
        
        fmt::print("Successfully initialized all Renderer2D resources\n");
    }

    void Renderer2D::begin()
    {
        m_vertexCount = 0;
        m_indexCount = 0;
        m_isDrawing = true;
        m_vertices.clear();
        m_indices.clear();
    }

    void Renderer2D::end()
    {
        flush();
    }

    void Renderer2D::flush()
    {
        if (m_vertexCount == 0 || m_vertices.empty() || m_indices.empty())
        {
            return;
        }

        // 更新顶点缓冲区
        const bgfx::Memory* vertexMem = bgfx::alloc(m_vertexCount * sizeof(Vertex));
        if (vertexMem == nullptr)
        {
            fmt::print("Failed to allocate vertex memory\n");
            return;
        }
        memcpy(vertexMem->data, m_vertices.data(), m_vertexCount * sizeof(Vertex));
        bgfx::update(m_vertexBuffer, 0, vertexMem);

        // 更新索引缓冲区
        const bgfx::Memory* indexMem = bgfx::alloc(m_indexCount * sizeof(uint16_t));
        memcpy(indexMem->data, m_indices.data(), m_indexCount * sizeof(uint16_t));
        bgfx::update(m_indexBuffer, 0, indexMem);

        // 设置渲染状态
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            BGFX_STATE_BLEND_ALPHA);

        // 设置变换矩阵
        float view[16];
        float proj[16];
        bx::mtxIdentity(view);
        bx::mtxOrtho(
            proj,
            0.0f,                                   // 左边界
            800.0f,                                 // 右边界
            600.0f,                                 // 底边界
            0.0f,                                   // 顶边界
            0.0f,                                   // 近平面
            1000.0f,                                // 远平面
            0.0f,                                   // 偏移
            bgfx::getCaps()->homogeneousDepth,     // 是否使用同质深度
            bx::Handedness::Right                   // 坐标系手性
        );

        float mvp[16];
        bx::mtxMul(mvp, view, proj);

        // 检查 uniform 是否有效
        if (m_u_modelViewProj.isValid()) {
            m_u_modelViewProj.setMatrix4(mvp);
        } else {
            fmt::print("Warning: MVP uniform is invalid, skipping matrix update\n");
        }

        // 提交绘制命令
        bgfx::setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount);
        bgfx::setIndexBuffer(m_indexBuffer, 0, m_indexCount);
        bgfx::setTexture(0, m_s_texture, m_currentTexture);
        bgfx::submit(0, m_program);

        // 重置计数器
        m_vertexCount = 0;
        m_indexCount = 0;
        m_vertices.clear();
        m_indices.clear();
        m_currentTexture = BGFX_INVALID_HANDLE;
    }

    void Renderer2D::drawRect(const Vector2f& position, const Vector2f& size, const Color& color)
    {
        if (!m_isDrawing) return;

        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES)
        {
            flush();
        }

        float x = position.x;
        float y = position.y;
        float w = size.x;
        float h = size.y;

        uint32_t colorValue = color.toABGR();

        // 添加顶点
        m_vertices.emplace_back(x, y, 0.0f, 0.0f, colorValue);
        m_vertices.emplace_back(x + w, y, 1.0f, 0.0f, colorValue);
        m_vertices.emplace_back(x + w, y + h, 1.0f, 1.0f, colorValue);
        m_vertices.emplace_back(x, y + h, 0.0f, 1.0f, colorValue);
        m_vertexCount += 4;

        // 添加索引
        uint16_t baseIndex = m_vertexCount - 4;
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 3);
        m_indexCount += 6;
    }

    void Renderer2D::drawCircle(const Vector2f& center, float radius, const Color& color, int segments)
    {
        if (!m_isDrawing) return;
        if (m_vertexCount + segments + 1 > MAX_VERTICES || m_indexCount + segments * 3 > MAX_INDICES)
        {
            flush();
        }

        // 添加中心顶点
        m_vertices.emplace_back(Vector2f(center.x, center.y), Vector2f(0.5f, 0.5f), color.toABGR());

        // 添加圆周顶点
        for (int i = 0; i < segments; ++i)
        {
            const auto angle = static_cast<float>(i * 2.0 * M_PI / segments);
            float x = center.x + radius * std::cos(angle);
            float y = center.y + radius * std::sin(angle);
            m_vertices.emplace_back(Vector2f(x, y), Vector2f(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)), color.toABGR());
        }

        // 添加索引
        uint16_t baseIndex = m_vertexCount - segments - 1;
        for (int i = 0; i < segments; ++i)
        {
            m_indices.push_back(baseIndex);
            m_indices.push_back(baseIndex + 1 + i);
            m_indices.push_back(baseIndex + 1 + ((i + 1) % segments));
            m_indexCount += 3;
        }
    }

    void Renderer2D::drawSprite(const Vector2f& position, const Vector2f& size, const TextureHandle& texture,
                                const Vector2f& uv0, const Vector2f& uv1, const Color& tint)
    {
        if (!m_isDrawing) return;
        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES)
        {
            flush();
        }

        uint32_t colorValue = tint.toABGR();
        uint16_t startVertex = m_vertexCount;

        // 添加顶点
        m_vertices.emplace_back(Vector2f(position.x, position.y), uv0, colorValue);
        m_vertices.emplace_back(Vector2f(position.x + size.x, position.y), uv1, colorValue);
        m_vertices.emplace_back(Vector2f(position.x + size.x, position.y + size.y), uv1, colorValue);
        m_vertices.emplace_back(Vector2f(position.x, position.y + size.y), uv0, colorValue);
        m_vertexCount += 4;

        // 添加索引
        uint16_t baseIndex = startVertex;
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 3);
        m_indexCount += 6;

        // 设置纹理
        m_currentTexture = texture; // 设置当前纹理，用于批处理
    }

    void Renderer2D::drawLine(const Vector2f& start, const Vector2f& end, float thickness, const Color& color)
    {
        if (!m_isDrawing) return;
        if (m_vertexCount + 4 > MAX_VERTICES || m_indexCount + 6 > MAX_INDICES)
        {
            flush();
        }

        Vector2f direction = Vector2f(end.x - start.x, end.y - start.y);
        float length = sqrt(direction.x * direction.x + direction.y * direction.y);
        if (length > 0)
        {
            direction.x /= length;
            direction.y /= length;
        }
        Vector2f normal(-direction.y, direction.x);
        normal = Vector2f(normal.x * thickness * 0.5f, normal.y * thickness * 0.5f);

        uint32_t colorValue = color.toABGR();
        uint16_t startVertex = m_vertexCount;

        // 添加顶点
        m_vertices.emplace_back(Vector2f(start.x - normal.x, start.y - normal.y), Vector2f(0.0f, 0.0f), colorValue);
        m_vertices.emplace_back(Vector2f(start.x + normal.x, start.y + normal.y), Vector2f(1.0f, 0.0f), colorValue);
        m_vertices.emplace_back(Vector2f(end.x + normal.x, end.y + normal.y), Vector2f(1.0f, 1.0f), colorValue);
        m_vertices.emplace_back(Vector2f(end.x - normal.x, end.y - normal.y), Vector2f(0.0f, 1.0f), colorValue);
        m_vertexCount += 4;

        // 添加索引
        uint16_t baseIndex = startVertex;
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 3);
        m_indexCount += 6;
    }

    void Renderer2D::drawTriangle(const Vector2f& p1, const Vector2f& p2, const Vector2f& p3, const Color& color)
    {
        if (!m_isDrawing) return;
        if (m_vertexCount + 3 > MAX_VERTICES || m_indexCount + 3 > MAX_INDICES)
        {
            flush();
        }

        uint32_t colorValue = color.toABGR();

        // 添加顶点
        m_vertices.emplace_back(p1.x, p1.y, 0.0f, 0.0f, colorValue);
        m_vertices.emplace_back(p2.x, p2.y, 1.0f, 0.0f, colorValue);
        m_vertices.emplace_back(p3.x, p3.y, 1.0f, 1.0f, colorValue);
        m_vertexCount += 3;

        // 添加索引
        uint16_t baseIndex = m_vertexCount - 3;
        m_indices.push_back(baseIndex + 0);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indexCount += 3;
    }
}
