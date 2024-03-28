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
	bool GlfwWindow::s_showStats = false;


	void GlfwWindow::initialize()
    {
        glfwSetErrorCallback(onErrorCallback);

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
		glfwSetWindowUserPointer(m_window,this);
		glfwSetKeyCallback(m_window, onKeyCallback);

		bgfx::renderFrame();

		bgfx::PlatformData platformData;
		platformData.nwh = glfwGetWin32Window(m_window);
		bgfx::setPlatformData(platformData);

		bgfx::Init init;
		init.platformData = platformData;

		int width, height;
		glfwGetWindowSize(m_window, &width, &height);
		init.resolution.width = static_cast<uint32_t>(width);
		init.resolution.height = static_cast<uint32_t>(height);
		init.resolution.reset = BGFX_RESET_VSYNC;
		if (!bgfx::init(init))
		{
			THROW_SIMPLE_EXCEPTION(GlfwError, "初始化异常错误");
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

		bgfx::setViewClear(clearViewId, BGFX_CLEAR_COLOR);
		bgfx::setViewRect(clearViewId, 0, 0, bgfx::BackbufferRatio::Equal);

		while (!glfwWindowShouldClose(m_window))
		{
			glfwPollEvents();
		
			int oldWidth = width, oldHeight = height;
			if (width != oldWidth || height != oldHeight) {
				bgfx::reset(static_cast<uint32_t>(width), static_cast<uint32_t>(height), BGFX_RESET_VSYNC);
				bgfx::setViewRect(clearViewId, 0, 0, bgfx::BackbufferRatio::Equal);
			}
			bgfx::touch(clearViewId);
			bgfx::dbgTextClear();
			bgfx::dbgTextPrintf(0, 0, 0x0f, "Press F1 to toggle stats.");
			bgfx::dbgTextPrintf(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");
			bgfx::dbgTextPrintf(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
			bgfx::dbgTextPrintf(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");
			const bgfx::Stats* stats = bgfx::getStats();
			bgfx::dbgTextPrintf(0, 2, 0x0f, "Backbuffer %dW x %dH in pixels, debug text %dW x %dH in characters.", stats->width, stats->height, stats->textWidth, stats->textHeight);
			// Enable stats or debug text.
			bgfx::setDebug(s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);
			// Advance to next frame. Process submitted rendering primitives.
			bgfx::frame();
			glfwSwapBuffers(m_window);
		
		}
		return EXIT_SUCCESS;
    }

    void GlfwWindow::shutdown()
    {
		glfwMakeContextCurrent(nullptr);
		bgfx::shutdown();
		glfwDestroyWindow(m_window);
		glfwTerminate();
    }


	void GlfwWindow::onKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (key == GLFW_KEY_F1 && action == GLFW_RELEASE)
			s_showStats = !s_showStats;
	}

	void GlfwWindow::onErrorCallback(int error, const char* description)
	{
        fprintf(stderr, "Error: %s\n", description);
	}

} // Tina
