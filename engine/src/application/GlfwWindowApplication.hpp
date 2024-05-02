//
// Created by wuxianggujun on 2024/4/28.
//

#ifndef TINA_APPLICATION_GLFWWINDOWAPPLICATION_HPP
#define TINA_APPLICATION_GLFWWINDOWAPPLICATION_HPP

#include <cstdint>
#include "framework/Application.hpp"

class GLFWwindow;

namespace Tina {

    class GlfwWindowApplication : public Application {
    protected:
        bool isKeyPressed(int32_t key);

        double getDeltaTime();

        virtual void afterCreateUiContext();
        virtual void beforeDestroyUiContext();

        virtual bool load();
        virtual void unLoad();
        virtual void renderScene(float deltaTime);
        virtual void renderUI(float deltaTime);
        virtual void update(float deltaTime);

    public:
        void run() override;

    protected:
        bool initialize() override;

        void close() override;

    private:
        GLFWwindow* _windowHandle = nullptr;
        void render(float deltaTime);
    };

} // Tina

#endif //TINA_APPLICATION_GLFWWINDOWAPPLICATION_HPP
