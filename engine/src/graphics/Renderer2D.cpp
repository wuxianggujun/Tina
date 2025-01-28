#include "graphics/Renderer2D.hpp"
#include "tool/BgfxUtils.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <fmt/format.h>

namespace Tina
{
    bgfx::VertexLayout PosColorVertex::ms_layout;

    Renderer2D::Renderer2D()
        : m_program(BGFX_INVALID_HANDLE)
        , m_mvpUniform(BGFX_INVALID_HANDLE)
    {
    }

    Renderer2D::~Renderer2D()
    {
        if (bgfx::isValid(m_mvpUniform))
        {
            bgfx::destroy(m_mvpUniform);
        }
        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }
    }

    void Renderer2D::initialize()
    {
        fmt::print("Initializing Renderer2D...\n");
        
        // 1. 首先初始化顶点布局
        PosColorVertex::init();
        fmt::print("Vertex layout initialized\n");

        // 2. 加载着色器程序
        fmt::print("Loading shader program...\n");
        m_program = BgfxUtils::loadProgram("sprite.vs", "sprite.fs");
        if (!bgfx::isValid(m_program)) {
            fmt::print("Failed to load shader program, handle: {}\n", m_program.idx);
            throw std::runtime_error("Failed to load shader program");
        }
        fmt::print("Successfully loaded shader program, handle: {}\n", m_program.idx);

        // 3. 等待几帧确保着色器程序完全加载
        for(int i = 0; i < 3; i++) {
            bgfx::frame();
        }
        fmt::print("Waited for shader program to load\n");

        // 4. 创建uniform
        fmt::print("Creating MVP uniform...\n");
        const char* mvpName = "u_modelViewProj";
        m_mvpUniform = bgfx::createUniform(mvpName, bgfx::UniformType::Mat4);
        if (!bgfx::isValid(m_mvpUniform)) {
            fmt::print("Failed to create MVP uniform '{}', handle: {}\n", mvpName, m_mvpUniform.idx);
            throw std::runtime_error("Failed to create MVP uniform");
        }
        fmt::print("Successfully created MVP uniform '{}', handle: {}\n", mvpName, m_mvpUniform.idx);

        // 5. 设置初始MVP矩阵
        float initialMvp[16];
        bx::mtxIdentity(initialMvp);
        bgfx::setUniform(m_mvpUniform, initialMvp);
        fmt::print("Renderer2D initialization completed\n");
    }

    void Renderer2D::setViewProjection(float width, float height)
    {
        if (!bgfx::isValid(m_mvpUniform)) {
            fmt::print("Warning: MVP uniform is invalid, skipping matrix update\n");
            return;
        }

        float view[16];
        float proj[16];
        bx::mtxIdentity(view);
        bx::mtxOrtho(
            proj,
            0.0f,                                   // 左边界
            width,                                  // 右边界
            height,                                 // 底边界
            0.0f,                                   // 顶边界
            0.0f,                                   // 近平面
            1000.0f,                                // 远平面
            0.0f,                                   // 偏移
            bgfx::getCaps()->homogeneousDepth,     // 是否使用同质深度
            bx::Handedness::Right                   // 坐标系手性
        );

        float mvp[16];
        bx::mtxMul(mvp, view, proj);
        bgfx::setUniform(m_mvpUniform, mvp);
    }
}
