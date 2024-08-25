#include "Shader.hpp"

#include "spdlog/common.h"

namespace Tina {
    Shader::~Shader() {
        m_Uniforms.clear();
        bgfx::destroy(m_ProgramHandle);
    }

    std::string Shader::getName() {
        return m_Name;
    }

    void Shader::loadShader(const std::string &name) {
        m_Name = name;
        std::string shaderPath;
        switch (bgfx::getRendererType()) {
            case bgfx::RendererType::Direct3D11:
            case bgfx::RendererType::Direct3D12:
                shaderPath = "shaders/d3d/";
                break;;
            case bgfx::RendererType::Vulkan:
                shaderPath = "shaders/spirv/";
                break;
            default:
                shaderPath = "shaders/metal/";
                break;
        }

        bx::FileReader vFileReader;
        bx::Error err;
        bx::FilePath vFilePath(std::string(shaderPath + name + ".vsb").c_str());
        if (!vFileReader.open(vFilePath, &err)) {
            printf("Failed to load %s.vsb: %s\n", name.c_str());
            return;
        }
        bx::FileReader fFileReader;
        bx::FilePath fFilePath(std::string(shaderPath + name + ".fsb").c_str());
        if (!fFileReader.open(fFilePath, &err)) {
            printf("Failed to load %s.fsb: %s\n", name.c_str());
            return;
        }

        bgfx::ShaderHandle vShaderHandle = bgfx::createShader(getMemory(vFileReader));
        bgfx::setName(vShaderHandle, (name + "_vs").c_str());

        bgfx::ShaderHandle fShaderHandle = bgfx::createShader(getMemory(fFileReader));
        bgfx::setName(fShaderHandle, (name + "_fs").c_str());

        m_ProgramHandle = bgfx::createProgram(vShaderHandle, fShaderHandle, true);
    }

    TinaVertexBufferHandle Shader::createVertexBuffer(const void *data, const uint32_t size,
                                                      const TinaVertexLayout &layout) {
        return bgfx::createVertexBuffer(bgfx::copy(data, size), layout);
    }

    TinaIndexBufferHandle Shader::createIndexBuffer(const void *data, uint32_t size) {
        return bgfx::createIndexBuffer(bgfx::copy(data, size));
    }

    void Shader::applyVertexBuffer(uint8_t stream, const TinaVertexBufferHandle &handle) {
        bgfx::setVertexBuffer(stream, handle);
    }

    void Shader::applyIndexBuffer(const TinaIndexBufferHandle &handle) {
        bgfx::setIndexBuffer(handle);
    }

    void Shader::submit(const bgfx::ViewId &viewID, const bool depth) {
        bgfx::submit(viewID, m_ProgramHandle, depth);
        bgfx::discard(0
                      BGFX_DISCARD_INDEX_BUFFER
                      | BGFX_DISCARD_VERTEX_STREAMS
                      | BGFX_DISCARD_BINDINGS
                      | BGFX_DISCARD_STATE
                      | BGFX_DISCARD_TRANSFORM
        );
    }

    bgfx::ProgramHandle &Shader::getProgramHandle() {
        return m_ProgramHandle;
    }

    void Shader::initUniform(const std::string &name, TinaUniformType type, uint16_t nmb) {
        m_Uniforms.push_back({bgfx::createUniform(name.c_str(), type, nmb), name});
    }

    void Shader::setUniform(const std::string &name, const void *data, uint16_t nmb) {
        for (auto &uniform: m_Uniforms) {
            if (uniform.tinaName.compare(name)) {
                bgfx::setUniform(uniform.tinaHandle, data, nmb);
                break;
            }
        }
    }

    TinaUniform *Shader::getUniform(std::string name) {
        for (auto &uniform: m_Uniforms) {
            if (uniform.tinaName.compare(name)) {
                return &uniform;
            }
        }
        return nullptr;
    }

    const bgfx::Memory *Shader::getMemory(bx::FileReader &fileReader) {
        bx::Error err;
        bx::seek(&fileReader, 0, bx::Whence::End);
        uint32_t size = bx::getSize(&fileReader);
        const bgfx::Memory *mem = bgfx::alloc(size + 1);
        bx::seek(&fileReader, 0, bx::Whence::Begin);
        bx::read(&fileReader, mem->data, size, &err);
        bx::close(&fileReader);
        mem->data[size] = '\0';
        return mem;
    }
}
