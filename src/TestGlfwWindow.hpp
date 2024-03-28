#include "GlfwWindow.hpp"

class TestGlfwWindow : public Tina::GlfwWindow{

public:

	int run() override{

		if (!glfwInitialized)
		{
			return EXIT_FAILURE;
		}

		while (!glfwWindowShouldClose(m_window))
		{
			glfwPollEvents();

			glfwSwapBuffers(m_window);

		}

		return 0;
	}
};