//
// Created by wuxianggujun on 2024/5/4.
//

#ifndef TINA_TOOL_GLFWTOOL_HPP
#define TINA_TOOL_GLFWTOOL_HPP

class GLFWwindow;

namespace Tina {

    class GlfwTool final{
     public:
        static void ErrorCallback(int error,const char* description);
        static void MouseButtonCallback(GLFWwindow* window,int button,int action,int mods);

     public:
        GlfwTool() = delete;
        ~GlfwTool() = delete;
        GlfwTool(const GlfwTool& glfwTool) = delete;
        GlfwTool& operator=(const GlfwTool& glfwTool) = delete;
    };

} // Tina

#endif //TINA_TOOL_GLFWTOOL_HPP
