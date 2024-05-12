//
// Created by wuxianggujun on 2024/5/4.
//

#include "GlfwTool.hpp"
#include "framework/log/Log.hpp"

namespace Tina {
    void GlfwTool::ErrorCallback(int error, const char *description) {
        LOG_TRACE(description);
    }
} // Tina