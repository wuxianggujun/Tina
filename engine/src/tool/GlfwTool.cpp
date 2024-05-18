//
// Created by wuxianggujun on 2024/5/4.
//

#include "GlfwTool.hpp"
#include "framework/log/Log.hpp"
#include <iostream>

namespace Tina {
    void GlfwTool::ErrorCallback(int error, const char *description) {
        //LOG_TRACE(description);
        if (error == 65545) return;
        std::cout << "Glfw Error: " << error << " : " << description << std::endl;
        //LOG_FMT_TRACE("GLFW Error {0} {1}",error,description);
    }

    void *GlfwTool::getGlfwNativeWindow() {
        return nullptr;
    }
} // Tina
