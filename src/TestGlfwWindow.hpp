#include "GlfwWindow.hpp"
#include <filesystem>
#include <fstream>
#include <entt/entt.hpp>

class TestGlfwWindow : public Tina::GlfwWindow{

public:

	bgfx::ShaderHandle loadShader(const std::string& shaderPath) {
		std::ifstream inputStream(shaderPath, std::ios::binary);
		if (!inputStream.is_open())
		{
			throw std::runtime_error("Failed to open a shader file.");
		}
		inputStream.seekg(0, std::ios::end);
		const long fileSize = inputStream.tellg();
		inputStream.seekg(0);
		const bgfx::Memory* mem = bgfx::alloc(fileSize + 1);
		inputStream.read(reinterpret_cast<char*>(mem->data), fileSize);
		mem->data[mem->size - 1] = '\0';
		return bgfx::createShader(mem);

	}


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