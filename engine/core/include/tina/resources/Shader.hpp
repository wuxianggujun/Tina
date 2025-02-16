#pragma once

#include "tina/resources/ResourceManager.hpp"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace Tina {

class Shader : public Resource {
public:
    Shader() : m_program(BGFX_INVALID_HANDLE) {}
    
    ~Shader() {
        release();
    }

    bool isValid() const override {
        return bgfx::isValid(m_program);
    }

    bool reload() override;

    void release() override {
        if (bgfx::isValid(m_program)) {
            bgfx::destroy(m_program);
            m_program = BGFX_INVALID_HANDLE;
        }
    }

    bgfx::ProgramHandle getHandle() const { return m_program; }

private:
    friend class ShaderLoader;
    bgfx::ProgramHandle m_program;
    std::string m_vertexPath;
    std::string m_fragmentPath;
};

class ShaderLoader : public IResourceLoader {
public:
    std::shared_ptr<Resource> load(const std::string& path) override;
    bool reload(Resource* resource) override;

private:
    bool loadShaderData(const std::string& vertexPath, const std::string& fragmentPath, Shader* shader);
    bgfx::ShaderHandle loadShaderFile(const std::string& path, bool isVertex);
};

}
