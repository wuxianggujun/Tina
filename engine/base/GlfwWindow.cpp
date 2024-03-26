//
// Created by 33442 on 2024/3/16.
//

#include "GlfwWindow.hpp"

#include <cstdlib>
#include <stdexcept>
#include <GL/gl.h>
#include <iostream>



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

		// ≥ı ºªØglfw
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		
		m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);

		if (!m_window)
		{
			glfwInitialized = false;
			throw std::runtime_error("Cannot create a window.");
		}
	
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
		return 0;
    }

    void GlfwWindow::shutdown()
    {
		glfwTerminate();
    }

	void GlfwWindow::ErrorCallback(int error, const char* description)
	{
        fprintf(stderr, "Error: %s\n", description);
	}

} // Tina
