#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <fmt/format.h>

namespace Tina
{
    bgfx::VertexLayout PosColorVertex::ms_layout;

    // 定义一个简单的矩形顶点数据
    static PosColorVertex s_quadVertices[] =
    {
        {-0.5f,  0.5f, 0.0f, 0xffffffff }, // 左上
        { 0.5f,  0.5f, 0.0f, 0xffffffff }, // 右上
        {-0.5f, -0.5f, 0.0f, 0xffffffff }, // 左下
        { 0.5f, -0.5f, 0.0f, 0xffffffff }, // 右下
    };

    // 定义索引数据
    static const uint16_t s_quadIndices[] =
    {
        0, 1, 2, // 第一个三角形
        1, 3, 2  // 第二个三角形
    };

    Renderer2D::Renderer2D()
        : m_program(BGFX_INVALID_HANDLE)
        , m_vbh(BGFX_INVALID_HANDLE)
        , m_ibh(BGFX_INVALID_HANDLE)
    {
    }

    Renderer2D::~Renderer2D()
    {
        if (bgfx::isValid(m_ibh))
        {
            bgfx::destroy(m_ibh);
        }
        if (bgfx::isValid(m_vbh))
        {
            bgfx::destroy(m_vbh);
        }
        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }
    }

    void Renderer2D::initialize()
    {
        fmt::print("Initializing Renderer2D...\n");
        
        // 1. 初始化顶点布局
        PosColorVertex::init();
        fmt::print("Vertex layout initialized\n");

        // 2. 创建顶点缓冲
        m_vbh = bgfx::createVertexBuffer(
            bgfx::makeRef(s_quadVertices, sizeof(s_quadVertices)),
            PosColorVertex::ms_layout
        );
        fmt::print("Vertex buffer created, handle: {}\n", m_vbh.idx);

        // 3. 创建索引缓冲
        m_ibh = bgfx::createIndexBuffer(
            bgfx::makeRef(s_quadIndices, sizeof(s_quadIndices))
        );
        fmt::print("Index buffer created, handle: {}\n", m_ibh.idx);

        // 4. 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("cubes.vs", "cubes.fs");
        if (!bgfx::isValid(m_program)) {
            fmt::print("Failed to load shader program, handle: {}\n", m_program.idx);
            throw std::runtime_error("Failed to load shader program");
        }
        fmt::print("Successfully loaded shader program, handle: {}\n", m_program.idx);

        fmt::print("Renderer2D initialization completed\n");
    }

    void Renderer2D::setViewProjection(float width, float height)
    {
        float view[16];
        float proj[16];

        bx::mtxIdentity(view);
        bx::mtxOrtho(
            proj,
            0.0f, width,   // 左右
            height, 0.0f,  // 上下（翻转Y轴）
            0.0f, 1000.0f, // 近远平面
            0.0f,
            bgfx::getCaps()->homogeneousDepth
        );

        // 设置视图变换
        bgfx::setViewTransform(0, view, proj);
        bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
    }

    void Renderer2D::render()
    {
        fmt::print("Starting render...\n");

        // 设置模型变换矩阵（这里使用单位矩阵）
        float mtx[16];
        bx::mtxIdentity(mtx);
        bgfx::setTransform(mtx);

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA;
        bgfx::setState(state);

        // 设置顶点和索引缓冲
        if (!bgfx::isValid(m_vbh)) {
            fmt::print("Warning: Invalid vertex buffer handle\n");
            return;
        }
        if (!bgfx::isValid(m_ibh)) {
            fmt::print("Warning: Invalid index buffer handle\n");
            return;
        }
        if (!bgfx::isValid(m_program)) {
            fmt::print("Warning: Invalid program handle\n");
            return;
        }

        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);

        // 确保视图被清除
        bgfx::touch(0);

        // 提交绘制命令
        bgfx::submit(0, m_program);

        fmt::print("Render completed\n");
    }
}
