//
// Created by wuxianggujun on 2024/5/4.
//

#include "GlfwTool.hpp"
#include "framework/log/Log.hpp"
#include <iostream>

/*
void Window::setIcon(const std::filesystem::path &iconPath) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc *data = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
    //ASSERT(data, "Failed to load image!");
    //ASSERT(channels == 4, "Icon must be RGBA");
    GLFWimage images[1];
    images[0].width = width;
    images[0].height = height;
    images[0].pixels = data;
    glfwSetWindowIcon(window, 1, images);
    stbi_image_free(data);
}
*/

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
