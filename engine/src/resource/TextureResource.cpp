#include "TextureResource.hpp"
#include "tool/BgfxUtils.hpp"

namespace Tina {
    TextureResource::TextureResource(const ResourceHandle &handle, const std::string &path):Resource(handle,path,ResourceType::Texture) {
    }

    TextureResource::~TextureResource() {
        TextureResource::unload();
    }

    bool TextureResource::load() {
        if (!isLoaded()) {
            m_texture = BgfxUtils::loadTexture(m_path.c_str()); 
        }
        return true;
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

    TextureHandle TextureResource::getTextureHandle() const {
        return m_texture.getHandle();
    }

}
