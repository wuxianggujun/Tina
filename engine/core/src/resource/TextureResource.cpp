#include "tina/resource/TextureResource.hpp"
#include "tina/log/Log.hpp"

namespace Tina {

TextureResource::TextureResource(const std::string& name, const std::string& path, const TextureSamplerConfig& config)
    : Resource(name, path)
    , m_config(config)
{
}

TextureResource::~TextureResource() {
    unload();
}

bool TextureResource::load() {
    // 实际加载由TextureLoader完成
    return true;
}

void TextureResource::unload() {
    if (bgfx::isValid(m_handle)) {
        bgfx::destroy(m_handle);
        m_handle = BGFX_INVALID_HANDLE;
    }

    m_width = 0;
    m_height = 0;
    m_state = ResourceState::Unloaded;
}

} // namespace Tina 