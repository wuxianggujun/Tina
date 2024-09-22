//
// Created by wuxianggujun on 2024/7/23.
//
#include "InputHandler.hpp"
#include "GLFWInput.hpp"
#include "GLFWWindow.hpp"

namespace Tina
{
    ScopePtr<InputHandler> createInputHandler(InputHandlerType type,Window* window)
    {
        switch (type)
        {
        case InputHandlerGLFW:
            return createScopePtr<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        case InputHandlerNONE:
        default:
            return createScopePtr<GLFWInput>(dynamic_cast<GLFWWindow*>(window));
        }
    }
}
