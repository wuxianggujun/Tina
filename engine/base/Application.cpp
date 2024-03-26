//
// Created by 33442 on 2024/3/16.
//

#include "Application.hpp"

#include "bgfx/bgfx.h"
#include <bx/math.h>
#include <GLFW/glfw3.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3native.h>
#include <glm/glm.hpp>
#include <fstream>
#include <cstring>

#include "GlfwWindow.hpp"
#include "Exception.hpp"
#include <iostream>
#include <boost/timer/timer.hpp>

namespace Tina
{
    Application::Application(const char* title, uint32_t width, uint32_t height)
    {
        this->mTitle = title;
        this->mWidth = width;
        this->mHeight = height;
    }

    void Application::initialize(int _argc, char** _argv)
    {
        bgfx::PlatformData platformData;
        memset(&platformData, 0, sizeof(platformData));
    }

    void Application::shutDown()
    {
        
    }


    int Application::run(int argc, char** argv)
    {
        //计算程序运行时间
        boost::timer::auto_cpu_timer startTime;

        initialize(argc, argv);

        GlfwWindow window("Window", 1280, 720);
        window.initialize();

		std::cout <<"GlfwWindow Initialize Time: "<< startTime.format() << std::endl;

        window.run();
		//try {
  //          THROW_SIMPLE_EXCEPTION(Tina::GlfwInitError, "An error occurred.");
		//}
		//catch (const Tina::Exception& e) {
  //          std::cerr << "Caught an exception: " << boost::diagnostic_information(e) << std::endl;
		//}
        window.shutdown();
        return 0;
    }
} // Tina
