//
// Created by wuxianggujun on 2024/7/23.
//
#include "InputHandler.hpp"
#include "GLFWInput.hpp"
#include "SDLInput.hpp"
#include "GLFWWindow.hpp"

namespace Tina
{
    class Window;
    
    Scope<InputHandler> createInputHandler(InputHandlerType type,Window* window)
    {
        switch (type)
        {
        case InputHandlerGLFW:
            return createScope<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        case InputHandlerSDL:
            return createScope<SDLInput>();
        case InputHandlerNONE:
        default:
            return createScope<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        }
    }
}
