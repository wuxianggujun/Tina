#pragma once

#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include "tina/core/Core.hpp"

namespace Tina::Render
{

    class TINA_CORE_API Renderer {
    public:
        Renderer();
        ~Renderer();

        bool initialize(int width, int height, const char* title);
        void shutdown();

        void beginFrame();
        void endFrame();

        // 获取 bgfx 视图 ID
        [[nodiscard]] bgfx::ViewId getMainViewId() const { return m_mainView; }

    private:
        bgfx::ViewId m_mainView;
    };

}
