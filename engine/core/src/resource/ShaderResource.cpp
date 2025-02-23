#include "tina/resource/ShaderResource.hpp"
#include "tina/log/Log.hpp"
#include "tina/utils/BgfxFormatter.hpp"

namespace Tina {

ShaderResource::ShaderResource(const std::string& name, const std::string& path)
    : Resource(name, path)
{
}

ShaderResource::~ShaderResource() {
    unload();
}

bool ShaderResource::load() {
    // 实际加载由ShaderLoader完成
    return true;
}

void ShaderResource::unload() {
    // 销毁uniform变量
    for (const auto& [name, info] : m_uniforms) {
        if (bgfx::isValid(info.handle)) {
            bgfx::destroy(info.handle);
        }
    }
    m_uniforms.clear();

    // 销毁着色器程序
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }

    m_state = ResourceState::Unloaded;
}

bgfx::UniformHandle ShaderResource::getUniform(const std::string& name, bgfx::UniformType::Enum type) {
    // 检查缓存
    auto it = m_uniforms.find(name);
    if (it != m_uniforms.end()) {
        if (it->second.type == type) {
            return it->second.handle;
        }
        else {
            TINA_ENGINE_WARN("Uniform '{}' type mismatch, expected {}, got {}", 
                name, it->second.type, type);
            return BGFX_INVALID_HANDLE;
        }
    }

    // 创建新的uniform变量
    bgfx::UniformHandle handle = bgfx::createUniform(name.c_str(), type);
    if (!bgfx::isValid(handle)) {
        TINA_ENGINE_ERROR("Failed to create uniform '{}'", name);
        return BGFX_INVALID_HANDLE;
    }

    // 缓存uniform信息
    m_uniforms[name] = {handle, type};
    return handle;
}

} // namespace Tina 