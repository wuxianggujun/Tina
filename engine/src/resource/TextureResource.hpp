#ifndef TINA_CORE_TEXTURE_RESOURCE_HPP
#define TINA_CORE_TEXTURE_RESOURCE_HPP

#include "Resource.hpp"
#include "graphics/Texture.hpp"

namespace Tina {
    
    class TextureResource : public Resource {
    public:
        TextureResource(const ResourceHandle& handle, const std::string& path);
        ~TextureResource() override;

        bool load() override;
        void unload() override;

        [[nodiscard]] TextureHandle getTextureHandle() const;
        [[nodiscard]] bool isLoaded() const override;
        [[nodiscard]] const Texture& getTexture() const;

        static constexpr ResourceType staticResourceType = ResourceType::Texture;
        
    protected:
        Texture m_texture;
    };
    
}



#endif