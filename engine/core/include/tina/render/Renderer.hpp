#pragma once

#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include "tina/core/Core.hpp"  // 包含核心头文件以获取导出宏

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
        bgfx::ViewId getMainViewId() const { return m_mainView; }

    private:
        bgfx::ViewId m_mainView;
    };

}
