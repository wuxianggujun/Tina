//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_HPP
#define TINA_WINDOW_HPP

#include "math/Vector.hpp"
#include "graphics/BgfxCallback.hpp"

namespace Tina {
    class Window {
    public:
        enum class WindowMode {
            WINDOWED,
            FULLSCREEN,
            BORDERLESS
        };

        class WindowConfig {
        public:
            const char *title{};
            Vector2i size;
            bool resizable{};
            bool fullscreen{};
            bool vsync{};
        };

    public:
        virtual ~Window() = default;

        virtual void create(WindowConfig config) = 0;

        virtual void destroy() = 0;

        virtual void render() = 0;

        virtual void pollEvents() = 0;

        virtual bool shouldClose() = 0;

        static void saveScreenShot(const std::string &fileName);

    protected:
        BgfxCallback m_bgfxCallback;
    };
} // Tina

#endif //TINA_WINDOW_HPP
