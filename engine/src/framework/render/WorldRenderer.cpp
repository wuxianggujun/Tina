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
        bgfx::setViewRect(viewId, 0, 0, bgfx::BackbufferRatio::Equal);
        bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f);
        bgfx::touch(viewId);

        bgfx::setState(
                BGFX_STATE_WRITE_R
                | BGFX_STATE_WRITE_G
                | BGFX_STATE_WRITE_B
                | BGFX_STATE_WRITE_A
        );

        bgfx::dbgTextPrintf(0, 1, 0x0f,
                            "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");

        bgfx::dbgTextPrintf(80, 1, 0x0f,
                            "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
        bgfx::dbgTextPrintf(80, 2, 0x0f,
                            "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");

        bgfx::frame();
    }

    bool WorldRenderer::isEnable() const {
        return Renderer::isEnable();
    }
} // Tina