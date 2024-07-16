//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_HPP
#define TINA_WINDOW_HPP

namespace Tina
{
    class Window
    {
    protected:
        class WindowConfig
        {
        public:
            const char* title;
            int width;
            int height;
            bool resizable;
            bool fullscreen;
            bool vsync;
        };

    public:
        virtual ~Window() = default;

        virtual void create(WindowConfig config) = 0;
        virtual void destroy() = 0;

        virtual void render() = 0;

        virtual void pollEvents() = 0;
        virtual bool shouldClose() = 0;
    };
} // Tina

#endif //TINA_WINDOW_HPP
