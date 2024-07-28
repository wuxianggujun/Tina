//
// Created by wuxianggujun on 2024/7/14.
//

#ifndef TINA_WINDOW_INPUT_HPP
#define TINA_WINDOW_INPUT_HPP

#include <string>
#include "core/Core.hpp"

class Window;

namespace Tina
{
    enum InputHandlerType
    {
        InputHandlerGLFW,
        InputHandlerSDL,
        InputHandlerNONE
    };

    class InputHandler
    {
    public:
        virtual ~InputHandler() = default;
        virtual void initialize() = 0;
        virtual void pollEvents() = 0;
        virtual void processEvent() = 0;
    };

    Scope<InputHandler> createInputHandler(InputHandlerType type,Window* window);
} // Tina

#endif //TINA_WINDOW_INPUT_HPP