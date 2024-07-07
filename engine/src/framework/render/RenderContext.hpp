//
// Created by wuxianggujun on 2024/5/19.
//

#ifndef TINA_RENDER_RENDERCONTEXT_HPP
#define TINA_RENDER_RENDERCONTEXT_HPP

#include "framework/Core.hpp"
#include "RenderTarget.hpp"
#include "framework/GraphicsBackend.hpp"

#include <bgfx/bgfx.h>


namespace Tina {
    class RenderContext {
    public:
        RenderContext() = default;

        RenderContext(const RenderContext &) = delete;

        RenderContext &operator=(const RenderContext &) = delete;

        RenderContext(RenderContext &&) = delete;

        RenderContext &operator=(RenderContext &&) = delete;

        ~RenderContext();

        void initialize(bgfx::Init& init, GraphicsBackend backend, void *hwd = nullptr);

        void onResize(uint16_t width, uint16_t height);

        void beginFrame();

        void submit(uint16_t viewID, uint16_t programHandle);

        void endFrame();

    private:
        uint16_t currentViewCount = 0;
        uint16_t backBufferWidth = 0;
        uint16_t backBufferHeight = 0;
    };
} // Tina

#endif //TINA_RENDER_RENDERCONTEXT_HPP
