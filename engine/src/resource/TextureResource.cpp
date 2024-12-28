#include "TextureResource.hpp"
#include "tool/BgfxUtils.hpp"

namespace Tina {
    TextureResource::TextureResource(const ResourceHandle &handle, const std::string &path):Resource(handle,path,ResourceType::Texture) {
    }

    TextureResource::~TextureResource() {
        unload();
    }

    bool TextureResource::load() {
        if (isLoaded()) {
            return true;
        }

        m_texture = BgfxUtils::loadTexture(m_path.c_str());
        
    }

    void TextureResource::unload() {
        if (isLoaded()) {
            bgfx::destroy(m_texture.getHandle());
            m_texture = Texture();
        }
    }

    bool TextureResource::isLoaded() const {
        return m_texture.isValid();
    }

    const Texture & TextureResource::getTexture() const {
        return m_texture;
    }
}
