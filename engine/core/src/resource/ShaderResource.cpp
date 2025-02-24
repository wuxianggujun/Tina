#include "tina/resource/ShaderResource.hpp"
#include "tina/core/RefPtr.hpp"
#include "bgfx/platform.h"
#include "tina/log/Log.hpp"
#include "tina/utils/BgfxFormatter.hpp"

namespace Tina {

RefPtr<ShaderResource> ShaderResource::create(const std::string& name, const std::string& path) {
    TINA_ENGINE_DEBUG("Creating ShaderResource '{}' at path: {}", name, path);
    return RefPtr<ShaderResource>(new ShaderResource(name, path));
}

ShaderResource::ShaderResource(const std::string& name, const std::string& path)
    : Resource(name, path)
    , m_program(BGFX_INVALID_HANDLE)
{
    TINA_ENGINE_DEBUG("ShaderResource constructor called for: {}, refCount: {}", m_name, getRefCount());
}

ShaderResource::~ShaderResource() {
    TINA_ENGINE_DEBUG("ShaderResource destructor called for: {}, refCount: {}, program valid: {}", 
        m_name, getRefCount(), bgfx::isValid(m_program));
    unload();
}

bool ShaderResource::isLoaded() const {
    bool baseLoaded = Resource::isLoaded();
    bool programValid = bgfx::isValid(m_program);
    TINA_ENGINE_DEBUG("ShaderResource::isLoaded check for: {}, base: {}, program: {}, refCount: {}", 
        m_name, baseLoaded, programValid, getRefCount());
    return baseLoaded && programValid;
}

bgfx::ProgramHandle ShaderResource::getProgram() const {
    TINA_ENGINE_DEBUG("Getting program handle for shader: {}, valid: {}, refCount: {}", 
        m_name, bgfx::isValid(m_program), getRefCount());
    return m_program;
}

bool ShaderResource::load() {
    TINA_ENGINE_DEBUG("ShaderResource::load called for: {}, refCount: {}", m_name, getRefCount());
    // 实际的加载在ShaderLoader中完成
    return true;
}

void ShaderResource::unload() {
    TINA_ENGINE_DEBUG("ShaderResource::unload called for: {}, program valid: {}, refCount: {}", 
        m_name, bgfx::isValid(m_program), getRefCount());
    
    if (bgfx::isValid(m_program)) {
        try {
            TINA_ENGINE_DEBUG("Destroying shader program for: {}", m_name);
            
            // 确保bgfx仍然有效
            if (!bgfx::getInternalData()->context) {
                TINA_ENGINE_WARN("bgfx context is invalid, skipping program destruction for: {}", m_name);
                m_program = BGFX_INVALID_HANDLE;
                return;
            }

            // 销毁所有相关的uniform
            for (auto& [name, info] : m_uniforms) {
                if (bgfx::isValid(info.handle)) {
                    bgfx::destroy(info.handle);
                }
            }
            m_uniforms.clear();

            // 销毁程序
            bgfx::destroy(m_program);
            m_program = BGFX_INVALID_HANDLE;
        }
        catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Exception during shader program destruction: {}", e.what());
            m_program = BGFX_INVALID_HANDLE;
        }
        catch (...) {
            TINA_ENGINE_ERROR("Unknown exception during shader program destruction");
            m_program = BGFX_INVALID_HANDLE;
        }
    }
    m_state = ResourceState::Unloaded;
    TINA_ENGINE_DEBUG("ShaderResource::unload completed for: {}, state: {}", m_name, (int)m_state);
}

bgfx::UniformHandle ShaderResource::getUniform(const std::string& name, bgfx::UniformType::Enum type) {
    // 检查缓存
    auto it = m_uniforms.find(name);
    if (it != m_uniforms.end()) {
        if (it->second.type == type) {
            return it->second.handle;
        }
        // 类型不匹配，销毁旧的
        if (bgfx::isValid(it->second.handle)) {
            bgfx::destroy(it->second.handle);
        }
        m_uniforms.erase(it);
    }

    // 创建新的uniform
    auto handle = bgfx::createUniform(name.c_str(), type);
    if (bgfx::isValid(handle)) {
        m_uniforms[name] = {handle, type};
    }
    return handle;
}

} // namespace Tina