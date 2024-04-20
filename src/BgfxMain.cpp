#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Log.hpp"
#include <format>
#include "core/View.hpp"
#include "core/Layer.hpp"
#include "core/Level.hpp"
#include "core/Window.hpp"

using namespace Tina;

std::vector<ColorVertex> v1{
  {-0.5f, -0.5f, 0xffffffff},
  {0.5f, -0.5f, 0xffffffff},
  {0.5f, 0.5f, 0xffffffff},
  {-0.5f, 0.5f, 0x00fffff}
};
std::vector<uint16_t> i1{
  0, 1, 2,
  2, 3, 0

};
std::vector<ColorVertex> v2{
  {-1.0f, 1.0f, 0xff00ff00},
  {-1.0f, 0.0f, 0x00ff00ff},
  {0.0f, 0.0f, 0x0f0f0f0f}
};
std::vector<uint16_t> i2{
  0, 1, 2
};



int test()
{
    Window window;
    View view;
    Layer layer1(v1, i1);

    view.addLayer(layer1);


    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window.window)) {
        //Calculate Deltatime
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;


        glfwPollEvents();
        window.handleResize();
        // Update game
        if (window.keyStates[GLFW_KEY_A]) {
            v1.at(0).xPos -= 0.1 * deltaTime;
            layer1.updateGeometry(v1);
        }
        if (window.keyStates[GLFW_KEY_D]) {
            v1.at(0).xPos += 0.1 * deltaTime;
            layer1.updateGeometry(v1);

        }
        // Update graphics
        view.submit();

        bgfx::frame();

    }
    bgfx::shutdown();
    glfwTerminate();

    return 0;
   /* LOG_LOGGER_INIT("logs/TinaEngine.log", Tina::STDOUT | Tina::FILEOUT | Tina::ASYNC);
    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);*/
}
