#include "Shader.hpp"

#include <fstream>
#include <iostream>

namespace Tina {
    Shader::Shader(const std::string &name) {
        loadFromFile(name);
    }

    Shader::~Shader() {
        destory();
    }

    void Shader::loadFromFile(const std::string &name) {
        std::string suffix;
        switch (bgfx::getRendererType()) {
            case bgfx::RendererType::OpenGL:
                suffix = "glsl";
                break;
            case bgfx::RendererType::OpenGLES:
                suffix = "essl";
                break;
            case bgfx::RendererType::Vulkan:
                suffix = "spv";
                break;
            case bgfx::RendererType::Metal:
                suffix = "mtl";
                break;
            case bgfx::RendererType::Direct3D11:
            case bgfx::RendererType::Direct3D12:
                suffix = "dx11";
                break;
            default:
                suffix = "dx10";
        }

        std::string vsPath = SHADER_PATH + suffix + "/" + name + ".vs.bin";
        std::string fsPath = SHADER_PATH + suffix + "/" + name + ".fs.bin";

        std::cout << "Loading vertex shader from: " << vsPath << std::endl;
        std::cout << "Loading fragment shader from: " << fsPath << std::endl;

        m_vertexShader = loadShader(vsPath);
        if (!bgfx::isValid(m_vertexShader)) {
            std::cerr << "Failed to load vertex shader: " << vsPath << std::endl;
            return;
        }

        m_fragmentShader = loadShader(fsPath);
        if (!bgfx::isValid(m_fragmentShader)) {
            std::cerr << "Failed to load fragment shader: " << fsPath << std::endl;
            return;
        }

        m_program = bgfx::createProgram(m_vertexShader, m_fragmentShader, true);
        if (!bgfx::isValid(m_program)) {
            std::cerr << "Failed to create shader program" << std::endl;
        }
    }

    bgfx::ShaderHandle Shader::loadShader(const std::string &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open shader file: " << path << std::endl;
            return BGFX_INVALID_HANDLE;
        }

        file.seekg(0, std::ios::end);
        uint32_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize == 0) {
            std::cerr << "Shader file is empty: " << path << std::endl;
            return BGFX_INVALID_HANDLE;
        }

        std::vector<char> data(fileSize);
        file.read(data.data(), fileSize);
        file.close();

        const bgfx::Memory* mem = bgfx::copy(data.data(), fileSize);
        return bgfx::createShader(mem);
    }

    bool Shader::isValid() const {
        return bgfx::isValid(m_program);
    }

    void Shader::destory() {
        if (bgfx::isValid(m_vertexShader)) {
            bgfx::destroy(m_vertexShader);
            m_vertexShader = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_fragmentShader)) {
            bgfx::destroy(m_fragmentShader);
            m_fragmentShader = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_program)) {
            bgfx::destroy(m_program);
            m_program = BGFX_INVALID_HANDLE;
        }
    }
}
