#include "tina/render/Renderer.hpp"
#include <bx/math.h>

namespace Tina::Render
{

    Renderer::Renderer()
        : m_mainView(0)
    {
    }

    Renderer::~Renderer() {
        shutdown();
    }

    bool Renderer::initialize(int width, int height, const char* title) {
        bgfx::Init init;
        init.type = bgfx::RendererType::Count; // 自动选择渲染后端
        init.resolution.width = width;
        init.resolution.height = height;
        init.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(init)) {
            return false;
        }

        // 设置调试标志
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        // 设置视图清除颜色
        bgfx::setViewClear(m_mainView
                           , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                           , 0x303030ff // 深灰色
                           , 1.0f
                           , 0
        );

        // 设置视图矩形
        bgfx::setViewRect(m_mainView, 0, 0, width, height);

        return true;
    }

    void Renderer::shutdown() {
        bgfx::shutdown();
    }

    void Renderer::beginFrame() {
        // 设置视图变换
        bgfx::touch(m_mainView);
    }

    void Renderer::endFrame() {
        bgfx::frame();
    }

}
