#ifndef TINA_CORE_GRAPHICS_TEXTURE_HPP
#define TINA_CORE_GRAPHICS_TEXTURE_HPP

#include <bgfx/bgfx.h>

namespace Tina {
    class Texture {
    public:
        Texture();
        explicit Texture(bgfx::TextureHandle handle);
        ~Texture() = default;

        void setTexture(bgfx::TextureHandle handle);
        bgfx::TextureHandle getTextureHandle() const;

        [[nodiscard]] bool isValid() const;

        Texture&operator=(const Texture &other);
        Texture&operator=(bgfx::TextureHandle handle) noexcept;
        
    private:
        bgfx::TextureHandle m_handle;
    };
}


#endif