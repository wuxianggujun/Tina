//
// Created by 33442 on 2024/3/16.
//

#include "GlfwWindow.hpp"

#include <cstdlib>
#include <stdexcept>
#include <GL/gl.h>
#include <iostream>
#include "Exception.hpp"


namespace Tina
{
    GlfwWindow::GlfwWindow(const char* title, int width, int height)
    {
        this->title = title;
        this->width = width;
        this->height = height;
    }

    void GlfwWindow::initialize()
    {
        glfwSetErrorCallback(ErrorCallback);

		if (!glfwInit())
		{
			glfwInitialized = false;
			throw std::runtime_error("Cannot initialize GLFW.");
		}

		// 初始化glfw
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		
		m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);

		if (!m_window)
		{
			glfwInitialized = false;
			throw std::runtime_error("Cannot create a window.");
		}

		glfwSetKeyCallback(m_window, KeyCallback);

		bgfx::renderFrame();

		bgfx::Init init;
		init.platformData.nwh = glfwGetWin32Window(m_window);

		int width, height;
		glfwGetWindowSize(m_window, &width, &height);
		init.resolution.width = static_cast<uint32_t>(width);
		init.resolution.height = static_cast<uint32_t>(height);
		init.resolution.reset = BGFX_RESET_VSYNC;
		//if (!bgfx::init(init))
		//{
			THROW_SIMPLE_EXCEPTION(GlfwError, "初始化异常错误");
		//}

		glfwMakeContextCurrent(m_window);
    }

    int GlfwWindow::run()
    {
		if (!glfwInitialized)
		{
			return EXIT_FAILURE;
		}

    	std::cout << "Hello, world!" << std::endl;

		while (!glfwWindowShouldClose(m_window))
		{
			glfwSwapBuffers(m_window);

			glfwPollEvents();

		}
		return EXIT_SUCCESS;
    }

    void GlfwWindow::shutdown()
    {
		glfwDestroyWindow(m_window);
		glfwTerminate();
    }

	void GlfwWindow::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
	{

	}

	void GlfwWindow::ErrorCallback(int error, const char* description)
	{
        fprintf(stderr, "Error: %s\n", description);
	}

} // Tina
