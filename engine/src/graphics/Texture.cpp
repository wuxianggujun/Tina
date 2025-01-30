#include "Texture.hpp"
#include <bimg/decode.h>
#include <bx/file.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/bx.h>

namespace Tina {
    
    static bx::DefaultAllocator s_allocator;
    static bx::Error s_error;

    Texture::Texture() : m_handle(BGFX_INVALID_HANDLE) {
    }

    Texture::Texture(const TextureHandle& handle) : m_handle(handle) {
    }

    Texture::~Texture() {
        if (bgfx::isValid(m_handle)) {
            bgfx::destroy(m_handle);
        }
    }

    void Texture::setHandle(const TextureHandle& handle) {
        if (bgfx::isValid(m_handle)) {
            bgfx::destroy(m_handle);
        }
        m_handle = handle;
    }

    TextureHandle Texture::getHandle() const {
        return m_handle;
    }

    bool Texture::isValid() const {
        return bgfx::isValid(m_handle);
    }

    uint16_t Texture::getIdx() const
    {
        return m_handle.idx;
    }

    bool Texture::operator==(const Texture& other) const {
        return m_handle.idx == other.m_handle.idx;
    }

    bool Texture::operator!=(const Texture& other) const {
        return m_handle.idx != other.m_handle.idx;
    }

    Texture& Texture::operator=(const Texture& other) {
        if (this != &other) {
            setHandle(other.m_handle);
        }
        return *this;
    }

    Texture& Texture::operator=(const TextureHandle& handle) {
        setHandle(handle);
        return *this;
    }

    TextureHandle Texture::loadFromFile(const std::string& filename) {
        bx::FileReader reader;
        if (!bx::open(&reader, filename.c_str(), &s_error)) {
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

    TextureHandle Texture::loadFromMemory(const void* data, uint32_t size) {
        bimg::ImageContainer* imageContainer = bimg::imageParse(&s_allocator, data, size);
        if (!imageContainer) {
            return BGFX_INVALID_HANDLE;
        }

        const bgfx::Memory* mem = bgfx::copy(imageContainer->m_data, imageContainer->m_size);

        TextureHandle handle = bgfx::createTexture2D(
            uint16_t(imageContainer->m_width),
            uint16_t(imageContainer->m_height),
            1 < imageContainer->m_numMips,
            imageContainer->m_numLayers,
            bgfx::TextureFormat::Enum(imageContainer->m_format),
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            mem
        );

        bimg::imageFree(imageContainer);
        return handle;
    }
}
