#include "Texture.hpp"

namespace Tina {
    Texture::Texture():m_handle(BGFX_INVALID_HANDLE) {
    }

    Texture::Texture(const bgfx::TextureHandle handle): m_handle(handle) {
    }

    void Texture::setTexture(bgfx::TextureHandle handle) {
        m_handle = handle;
    }

    bgfx::TextureHandle Texture::getTextureHandle() const {
        return m_handle;
    }
    bool Texture::isValid() const {
        return bgfx::isValid(m_handle);
    }

    Texture & Texture::operator=(const Texture &other) {
        if (this != &other) {
            m_handle = other.m_handle;
        }
        return *this;
    }

    Texture & Texture::operator=(const bgfx::TextureHandle handle) noexcept {
        m_handle = handle;
        return *this;
    }
}
