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
    
    ScopePtr<InputHandler> createInputHandler(InputHandlerType type,Window* window)
    {
        switch (type)
        {
        case InputHandlerGLFW:
            return createScopePtr<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        case InputHandlerSDL:
            return createScopePtr<SDLInput>();
        case InputHandlerNONE:
        default:
            return createScopePtr<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        }
    }
}
