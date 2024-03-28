#include "GlfwWindow.hpp"

class TestGlfwWindow : public Tina::GlfwWindow{

public:


	void render() {

	}

	int run() override{

		if (!glfwInitialized)
		{
			return EXIT_FAILURE;
		}

		while (!glfwWindowShouldClose(m_window))
		{
			glfwPollEvents();
			bgfx::touch(0);

			render();

			bgfx::frame();
		}

		return 0;
	}



private:
	bgfx::ShaderHandle vertex_shader;
	bgfx::ShaderHandle fragment_shader;
	bgfx::ProgramHandle programHandle;
};