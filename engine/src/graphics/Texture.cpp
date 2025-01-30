#include "Texture.hpp"
#include <bimg/decode.h>
#include <bx/file.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/bx.h>

namespace Tina
{
    static bx::DefaultAllocator s_allocator;
    static bx::Error s_error;

    Texture::Texture() : m_textureData(nullptr)
    {
    }


    Texture::Texture(const TextureHandle& handle)
        : m_textureData(bgfx::isValid(handle) ? new TextureData(handle) : nullptr)
    {
    }

    Texture::~Texture()
    {
        release();
    }

    Texture::Texture(const Texture& other) : m_textureData(other.m_textureData)
    {
        addRef();
    }

    Texture& Texture::operator=(const Texture& other)
    {
        if (this != &other)
        {
            release();
            m_textureData = other.m_textureData;
            addRef();
        }
        return *this;
    }

    Texture::Texture(Texture&& other) noexcept : m_textureData(other.m_textureData)
    {
        other.m_textureData = nullptr;
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_textureData = other.m_textureData;
            other.m_textureData = nullptr;
        }
        return *this;
    }


    void Texture::setHandle(const TextureHandle& handle)
    {
        if (!m_textureData || m_textureData->handle.idx != handle.idx)
        {
            release();
            if (bgfx::isValid(handle))
            {
                m_textureData = new TextureData(handle);
            }
        }
    }

    TextureHandle Texture::getHandle() const
    {
        if (m_textureData)
        {
            return m_textureData->handle;
        }
        TextureHandle invalid = {BGFX_INVALID_HANDLE}; // 正确创建无效句柄
        return invalid;
    }


    bool Texture::isValid() const
    {
        return m_textureData && bgfx::isValid(m_textureData->handle);
    }

    uint16_t Texture::getIdx() const
    {
        if (m_textureData && bgfx::isValid(m_textureData->handle))
            return m_textureData->handle.idx;
        return UINT16_MAX; // 使用 UINT16_MAX 作为无效索引
    }


    void Texture::addRef()
    {
        if (m_textureData)
        {
            ++m_textureData->refCount;
        }
    }

    void Texture::release()
    {
        if (m_textureData)
        {
            if (--m_textureData->refCount == 0)
            {
                delete m_textureData;
            }
            m_textureData = nullptr;
        }
    }

    bool Texture::operator==(const Texture& other) const
    {
        if (m_textureData == nullptr && other.m_textureData == nullptr) return true;
        if (m_textureData == nullptr || other.m_textureData == nullptr) return false;
        return m_textureData->handle.idx == other.m_textureData->handle.idx;
    }

    bool Texture::operator!=(const Texture& other) const
    {
        return !(*this == other);
    }


    Texture& Texture::operator=(const TextureHandle& handle)
    {
        setHandle(handle);
        return *this;
    }

    TextureHandle Texture::loadFromFile(const std::string& filename)
    {
        bx::FileReader reader;
        if (!bx::open(&reader, filename.c_str(), &s_error))
        {
            return BGFX_INVALID_HANDLE;
        }

        const uint32_t size = static_cast<uint32_t>(bx::getSize(&reader));
        void* data = bx::alloc(&s_allocator, size);
        bx::read(&reader, data, size, &s_error);
        bx::close(&reader);

        const TextureHandle handle = loadFromMemory(data, size);
        bx::free(&s_allocator, data);

        return handle;
    }

    TextureHandle Texture::loadFromMemory(const void* data, uint32_t size)
    {
        bimg::ImageContainer* imageContainer = bimg::imageParse(&s_allocator, data, size);
        if (!imageContainer)
        {
            return BGFX_INVALID_HANDLE;
        }

        const bgfx::Memory* mem = bgfx::copy(imageContainer->m_data, imageContainer->m_size);

        const TextureHandle handle = bgfx::createTexture2D(
            static_cast<uint16_t>(imageContainer->m_width),
            static_cast<uint16_t>(imageContainer->m_height),
            1 < imageContainer->m_numMips,
            imageContainer->m_numLayers,
            static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            mem
        );

        bimg::imageFree(imageContainer);
        return handle;
    }
}
