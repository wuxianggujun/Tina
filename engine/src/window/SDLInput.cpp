//
// Created by wuxianggujun on 2024/7/23.
//

#include "SDLInput.hpp"

namespace Tina {
    void SDLInput::initialize()
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            printf("SDL initialization failed: %s\n", SDL_GetError());
            return;
        }
    }

    void SDLInput::pollEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            handleEvent(event);
        }
    }

    void SDLInput::processEvent()
    {
        
    }

    void SDLInput::handleEvent(const ::SDL_Event& event)
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            printf("SDL Key event: key=%d, type=%d\n", event.key.type, event.type);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            printf("SDL Mouse button event: button=%d, type=%d\n", event.button.button, event.type);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            printf("SDL Mouse motion event: xpos=%d, ypos=%d\n", event.motion.x, event.motion.y);
            break;
        default:
            break;
        }
    }
} // Tina

