//
// Created by wuxianggujun on 2024/5/19.
//

#include "WorldRenderer.hpp"

namespace Tina {

    void WorldRenderer::initialize() {

    }

    void WorldRenderer::update(const float *viewMatrix, const float *projectionMatrix) {

    }

    void WorldRenderer::render(float deltaTime) {
        bgfx::setViewRect(viewId, 0, 0, bgfx::BackbufferRatio::Equal);
        bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f);
        bgfx::touch(viewId);

        bgfx::setState(
                BGFX_STATE_WRITE_R
                | BGFX_STATE_WRITE_G
                | BGFX_STATE_WRITE_B
                | BGFX_STATE_WRITE_A
        );

    }

    bool WorldRenderer::isEnable() const {
        return Renderer::isEnable();
    }
} // Tina