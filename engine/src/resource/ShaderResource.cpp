#include "ShaderResource.hpp"

namespace Tina {
    ShaderResource::ShaderResource(const ResourceHandle &handle, const std::string &path):Resource(handle,path,ResourceType::Shader) {
        
    }

    ShaderResource::~ShaderResource() {
        unload();
    }

    bool ShaderResource::load() {
        if (isLoaded()) {
            return true;
        }

        m_shader.loadFromFile(m_path);
        return m_shader.isValid();
    }

    void ShaderResource::unload() {
        if (isLoaded()) {
            m_shader.destory();
        }
    }

    bool ShaderResource::isLoaded() const {
        return m_shader.isValid();
    }

    const Shader & ShaderResource::getShader() const {
        return m_shader;
    }
}
