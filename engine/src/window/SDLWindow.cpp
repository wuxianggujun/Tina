//
// Created by wuxianggujun on 2024/7/12.
//

#include "SDLWindow.hpp"

namespace Tina
{
    SDLWindow::SDLWindow(): m_window(nullptr, &SDL_DestroyWindow)
    {
    }

    SDLWindow::~SDLWindow()
    {
        destroy();
    }

    void SDLWindow::render()
    {
    }

    void SDLWindow::create(WindowConfig config)
    {
        if (!make_sdlsystem(SDL_INIT_VIDEO))
        {
            printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
            exit(1);
        }
        m_window = make_window(config.title, config.size.width, config.size.height,SDL_WINDOW_BORDERLESS);;
        if (!m_window)
        {
            printf("Window could not be created! SDL Error: %s\n", SDL_GetError());
            exit(1);
        }
    }

    void SDLWindow::destroy()
    {
    }

    void SDLWindow::pollEvents()
    {
        SDL_Event event;
        if (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                exit(0);
            }
        }
    }

    bool SDLWindow::shouldClose()
    {
        return true;
    }
} // Tina
