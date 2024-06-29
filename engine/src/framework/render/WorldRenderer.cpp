//
// Created by wuxianggujun on 2024/5/19.
//

#include "WorldRenderer.hpp"

namespace Tina {
    void WorldRenderer::initialize() {
        // Set view 0 clear state.
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0
        );
    }

    void WorldRenderer::update(const float *viewMatrix, const float *projectionMatrix) {
    }

    void WorldRenderer::render(float deltaTime) {
        bgfx::touch(viewId);
        bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x333399FF);


        //绘图操作

        bgfx::frame();
    }

    bool WorldRenderer::isEnable() const {
        return Renderer::isEnable();
    }
} // Tina
