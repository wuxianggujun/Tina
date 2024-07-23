//
// Created by wuxianggujun on 2024/7/23.
//

#ifndef TINA_WINDOW_SDLINPUT_HPP
#define TINA_WINDOW_SDLINPUT_HPP


#include <SDL3/SDL.h>
#include "InputHandler.hpp"


namespace Tina
{
    class SDLInput final : public InputHandler
    {
    public:
        void initialize() override;
        void pollEvents() override;
        void processEvent() override;

    private:
        void handleEvent(const SDL_Event& event);
    };
} // Tina

#endif //TINA_WINDOW_SDLINPUT_HPP
