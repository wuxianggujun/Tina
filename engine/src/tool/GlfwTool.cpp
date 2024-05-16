//
// Created by wuxianggujun on 2024/5/4.
//

#include "GlfwTool.hpp"
#include "framework/log/Log.hpp"
#include "GLFW/glfw3.h"
#include <iostream>

namespace Tina {
    void GlfwTool::ErrorCallback(int error, const char *description) {
        //LOG_TRACE(description);
        if(error == 65545) return;
        LOG_FMT_TRACE("GLFW Error {0} {1}",error,description);
    }
} // Tina
