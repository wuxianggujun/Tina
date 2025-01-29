#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace Tina {
    using TextureHandle = bgfx::TextureHandle;

    class Texture {
    public:
        Texture();
        explicit Texture(const TextureHandle& handle);
        ~Texture();

        void setHandle(const TextureHandle& handle);
        [[nodiscard]] TextureHandle getHandle() const;
        [[nodiscard]] bool isValid() const;

        static TextureHandle loadFromFile(const std::string& filename);
        static TextureHandle loadFromMemory(const void* data, uint32_t size);

        Texture& operator=(const Texture& other);
        Texture& operator=(const TextureHandle& handle);

    private:
        TextureHandle m_handle;
    };

    // 全局函数用于加载纹理
    inline TextureHandle loadTexture(const char* filename) {
        return Texture::loadFromFile(std::string(filename));
    }
}