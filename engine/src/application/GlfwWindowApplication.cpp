//
// Created by wuxianggujun on 2024/4/28.
//

#include "GlfwWindowApplication.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace Tina {
    void GlfwWindowApplication::run() {
        if (!initialize()){
            return;
        }

        if (!load()){
            return;
        }

        double previousTime = glfwGetTime();
        while (!glfwWindowShouldClose(_windowHandle)){
            double currentTime = glfwGetTime();
            auto deltaTime = static_cast<float>(currentTime - previousTime);
            previousTime = currentTime;

            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glClearColor(49.f/255,77.f/255,121.f/255,1.f);

            glfwPollEvents();
            update(deltaTime);
            render(deltaTime);
        }
        unLoad();
    }

    bool GlfwWindowApplication::initialize() {
        if (!glfwInit()){
            return false;
        }

        glfwWindowHint(GLFW_DECORATED,GLFW_TRUE);
        glfwWindowHint(GLFW_CLIENT_API,GLFW_OPENGL_API);
        glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,6);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR,GLFW_TRUE);

        const auto primaryMonitor = glfwGetPrimaryMonitor();
        const auto primaryMonitorVideoMode = glfwGetVideoMode(primaryMonitor);

        constexpr int windowWidth = 1920;
        constexpr int windowHeight = 1080;

        _windowHandle = glfwCreateWindow(windowWidth,windowHeight,"Project Template", nullptr, nullptr);
        if (_windowHandle == nullptr){
            glfwTerminate();
            return false;
        }

        const auto screenWidth = primaryMonitorVideoMode->width;
        const auto screenHeight = primaryMonitorVideoMode->height;
        glfwSetWindowPos(_windowHandle, (screenWidth - windowWidth) / 2, (screenHeight - windowHeight) / 2);

        glfwMakeContextCurrent(_windowHandle);
        gladLoadGL(glfwGetProcAddress);



        afterCreateUiContext();

        return true;
    }

    bool GlfwWindowApplication::load() {
        /* Render here */
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glClearColor(49.f/255,77.f/255,121.f/255,1.f);
        glfwSwapInterval(1);
        return true;
    }

    void GlfwWindowApplication::close() {
        glfwSetWindowShouldClose(_windowHandle, false);
    }

    bool GlfwWindowApplication::isKeyPressed(int32_t key) {
        return glfwGetKey(_windowHandle,key) == GLFW_PRESS;
    }

    void GlfwWindowApplication::unLoad() {
    beforeDestroyUiContext();
    glfwTerminate();
    }

    void GlfwWindowApplication::render(float deltaTime) {
        renderScene(deltaTime);
        glfwSwapBuffers(_windowHandle);
    }

    void GlfwWindowApplication::afterCreateUiContext() {

    }

    void GlfwWindowApplication::beforeDestroyUiContext() {

    }

    void GlfwWindowApplication::renderScene(float deltaTime) {

    }

    void GlfwWindowApplication::renderUI(float deltaTime) {

    }

    void GlfwWindowApplication::update(float deltaTime) {

    }

    double GlfwWindowApplication::getDeltaTime() {
        return 0;
    }


} // Tina