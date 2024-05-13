//
// Created by wuxianggujun on 2024/5/4.
//

#include "GlfwTool.hpp"
#include "framework/log/Log.hpp"
#include <iostream>

namespace Tina {
    void GlfwTool::ErrorCallback(int error, const char *description) {
        //LOG_TRACE(description);
        std::cout << "Glfw error: " <<"(" << error << ") " << description << std::endl;
    }
} // Tina
