//
// Created by wuxianggujun on 2024/7/12.
// https://gist.github.com/wuxianggujun/549c8cbcc8c6b6a751daac493624617d
//

#ifndef TINA_WINDOW_SDLWINDOW_HPP
#define TINA_WINDOW_SDLWINDOW_HPP

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <bx/timer.h>
#include <bx/thread.h>

#include "tool/SDL3.hpp"
#include "Window.hpp"
#include "core/Core.hpp"

namespace Tina
{
    class SDLWindow final : public Window
    {
    public:
        SDLWindow();
        ~SDLWindow() override;
        void render() override;
        void create(WindowConfig config) override;
        void destroy() override;
        void pollEvents() override;
        bool shouldClose() override;

    private:
        window_ptr_t m_window;
        bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle m_ibh = BGFX_INVALID_HANDLE;

        float m_camPitch = 0.0f;
        float m_camYaw = 0.0f;
        float m_rotScale = 0.0f;

        int m_prevMouseX = 0;
        int m_prevMouseY = 0;

        int m_width = 0;
        int m_height = 0;
        bool m_quit = false;
    };
} // Tina

#endif //TINA_WINDOW_SDLWINDOW_HPP
