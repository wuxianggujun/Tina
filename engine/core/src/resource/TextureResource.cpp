#include "tina/resource/TextureResource.hpp"
#include <bgfx/platform.h>
#include "tina/log/Log.hpp"

namespace Tina {

RefPtr<TextureResource> TextureResource::create(const std::string& name, const std::string& path) {
    TINA_ENGINE_DEBUG("Creating TextureResource '{}' at path: {}", name, path);
    return RefPtr<TextureResource>(new TextureResource(name, path));
}

TextureResource::TextureResource(const std::string& name, const std::string& path)
    : Resource(name, path)
{
    TINA_ENGINE_DEBUG("TextureResource constructor called for: {}, refCount: {}", 
        m_name, getRefCount());
}

TextureResource::~TextureResource() {
    TINA_ENGINE_DEBUG("TextureResource destructor called for: {}, refCount: {}, handle valid: {}", 
        m_name, getRefCount(), bgfx::isValid(m_handle));
    unload();
}

bool TextureResource::isLoaded() const {
    bool baseLoaded = Resource::isLoaded();
    bool handleValid = bgfx::isValid(m_handle);
    // TINA_ENGINE_DEBUG("TextureResource::isLoaded check for: {}, base: {}, handle: {}, refCount: {}", 
    //     m_name, baseLoaded, handleValid, getRefCount());
    return baseLoaded && handleValid;
}

void TextureResource::setSamplerFlags(uint32_t flags) {
    if (m_flags != flags) {
        m_flags = flags;
    }
}

bool TextureResource::load() {
    TINA_ENGINE_DEBUG("TextureResource::load called for: {}, refCount: {}", 
        m_name, getRefCount());
    // 实际加载在TextureLoader中完成
    return true;
}

void TextureResource::unload() {
    TINA_ENGINE_DEBUG("TextureResource::unload called for: {}, handle valid: {}, refCount: {}", 
        m_name, bgfx::isValid(m_handle), getRefCount());

    if (bgfx::isValid(m_handle)) {
        try {
            TINA_ENGINE_DEBUG("Destroying texture handle for: {}", m_name);
            
            // 确保bgfx仍然有效
            if (!bgfx::getInternalData()->context) {
                TINA_ENGINE_WARN("bgfx context is invalid, skipping texture destruction for: {}", m_name);
                m_handle = BGFX_INVALID_HANDLE;
                return;
            }

            // 销毁纹理
            bgfx::destroy(m_handle);
            m_handle = BGFX_INVALID_HANDLE;
        }
        catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Exception during texture handle destruction: {}", e.what());
            m_handle = BGFX_INVALID_HANDLE;
        }
        catch (...) {
            TINA_ENGINE_ERROR("Unknown exception during texture handle destruction");
            m_handle = BGFX_INVALID_HANDLE;
        }
    }

    m_width = 0;
    m_height = 0;
    m_format = bgfx::TextureFormat::Unknown;
    m_flags = BGFX_TEXTURE_NONE;
    m_state = ResourceState::Unloaded;

    TINA_ENGINE_DEBUG("TextureResource::unload completed for: {}, state: {}", 
        m_name, (int)m_state);
}
} 