#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include "core/Core.hpp"

namespace Tina
{
    using TextureHandle = bgfx::TextureHandle;

    class Texture
    {
    public:
        Texture();
        explicit Texture(const TextureHandle& handle);
        ~Texture();

        // 允许拷贝
        Texture(const Texture& other);
        Texture& operator=(const Texture& other);


        // 允许移动
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        void setHandle(const TextureHandle& handle);
        [[nodiscard]] TextureHandle getHandle() const;
        [[nodiscard]] bool isValid() const;
        [[nodiscard]] uint16_t getIdx() const;

        static TextureHandle loadFromFile(const std::string& filename);
        static TextureHandle loadFromMemory(const void* data, uint32_t size);

        // TextureHandle 赋值运算符
        Texture& operator=(const TextureHandle& handle);
        bool operator==(const Texture& other) const;
        bool operator!=(const Texture& other) const;

    private:
        struct TextureData
        {
            TextureHandle handle;
            int refCount;

            explicit TextureData(TextureHandle texture_handle): handle(texture_handle), refCount(1)
            {
            }

            ~TextureData()
            {
                if (bgfx::isValid(handle))
                {
                    bgfx::destroy(handle);
                }
            }
        };

        TextureData* m_textureData;

        void addRef();
        void release();
    };

    // 全局函数用于加载纹理
    inline TextureHandle loadTexture(const char* filename)
    {
        return Texture::loadFromFile(std::string(filename));
    }
}
