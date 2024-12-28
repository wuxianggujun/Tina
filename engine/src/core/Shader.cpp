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

        m_vertexShader = loadShader(SHADER_PATH + suffix + "/" + name + ".vs.bin");
        m_fragmentShader = loadShader(SHADER_PATH + suffix + "/" + name + ".fs.bin");

        m_program = bgfx::createProgram(m_vertexShader, m_fragmentShader, true);
    }

    bgfx::ShaderHandle Shader::loadShader(const std::string &path) {
        std::ifstream file(path, std::ios::binary);
        file.seekg(0, std::ios::end);
        uint32_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string str;
        str.reserve(fileSize);
        str.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        return bgfx::createShader(bgfx::copy(str.data(), fileSize));
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
