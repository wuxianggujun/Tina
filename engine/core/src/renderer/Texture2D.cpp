//
// Created by wuxianggujun on 2025/2/11.
//

#include "tina/renderer/Texture2D.hpp"
#include <cassert>
#include <array>
#include <tina/core/Core.hpp>
#include "tina/log/Logger.hpp"
#include <algorithm>

#include "bgfx/platform.h"

namespace Tina
{
    constexpr std::array<bgfx::TextureFormat::Enum, static_cast<size_t>(Format::RGBA4) + 1> k_TextureFormatsBgfx{
        bgfx::TextureFormat::BC1, // BlockCompression1
        bgfx::TextureFormat::BC2, // BlockCompression2
        bgfx::TextureFormat::BC3, // BlockCompression3
        bgfx::TextureFormat::D24S8, // Depth24Stencil8
        bgfx::TextureFormat::D16, // DepthComponent16
        bgfx::TextureFormat::D24, // DepthComponent24
        bgfx::TextureFormat::R16F, // R16F
        bgfx::TextureFormat::R16I, // R16I
        bgfx::TextureFormat::R16S, // R16SNorm
        bgfx::TextureFormat::R16U, // R16UI
        bgfx::TextureFormat::R32F, // R32F
        bgfx::TextureFormat::R32I, // R32I
        bgfx::TextureFormat::R32U, // R32UI
        bgfx::TextureFormat::A8, // R8
        bgfx::TextureFormat::R8, // R8
        bgfx::TextureFormat::R8I, // R8I
        bgfx::TextureFormat::R8S, // R8SNorm
        bgfx::TextureFormat::R8U, // R8UI
        bgfx::TextureFormat::RG16, // RG16
        bgfx::TextureFormat::RG16F, // RG16F
        bgfx::TextureFormat::RG16S, // RG16SNorm
        bgfx::TextureFormat::RG32F, // RG32F
        bgfx::TextureFormat::RG32I, // RG32I
        bgfx::TextureFormat::RG32U, // RG32UI
        bgfx::TextureFormat::RG8, // RG8
        bgfx::TextureFormat::RG8I, // RG8I
        bgfx::TextureFormat::RG8S, // RG8SNorm
        bgfx::TextureFormat::RG8U, // RG8UI
        bgfx::TextureFormat::RGB10A2, // RGB10A2
        bgfx::TextureFormat::B5G6R5, // B5G6R5
        bgfx::TextureFormat::R5G6B5, // R5G6B5
        bgfx::TextureFormat::BGR5A1, // BGBRA1
        bgfx::TextureFormat::RGB5A1, // RGB5A1
        bgfx::TextureFormat::RGB8, // RGB8
        bgfx::TextureFormat::RGB8I, // RGB8I
        bgfx::TextureFormat::RGB8U, // RGB8UI
        bgfx::TextureFormat::RGB9E5F, // RGB9E5
        bgfx::TextureFormat::RGBA8, // RGBA8
        bgfx::TextureFormat::RGBA8I, // RGBA8I
        bgfx::TextureFormat::RGBA8U, // RGBA8UI
        bgfx::TextureFormat::RGBA8S, // RGBA8SNorm
        bgfx::TextureFormat::BGRA8, // BGRA8
        bgfx::TextureFormat::RGBA16, // RGBA16
        bgfx::TextureFormat::RGBA16F, // RGBA16F
        bgfx::TextureFormat::RGBA16I, // RGBA16I
        bgfx::TextureFormat::RGBA16U, // RGBA16UI
        bgfx::TextureFormat::RGBA16S, // RGBA16SNorm
        bgfx::TextureFormat::RGBA32F, // RGBA32F
        bgfx::TextureFormat::RGBA32I, // RGBA32I
        bgfx::TextureFormat::RGBA32U, // RGBA32UI
        bgfx::TextureFormat::BGRA4, // BGRA4
        bgfx::TextureFormat::RGBA4, // RGBA4
    };

    static bgfx::TextureFormat::Enum getBgfxTextureFormat(Format format)
    {
        return k_TextureFormatsBgfx.at(static_cast<size_t>(format));
    }

    Texture2D::Texture2D(std::string name)
        : name(std::move(name))
          , handle(BGFX_INVALID_HANDLE), info()
    {
    }

    Texture2D::~Texture2D()
    {
        TINA_PROFILE_FUNCTION();
        destroy();
    }

    void Texture2D::create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                           Wrapping wrapping, Filter filter, const bgfx::Memory* memory) noexcept
    {
        uint64_t flags = BGFX_TEXTURE_NONE;
        switch (wrapping)
        {
        case Wrapping::ClampEdge:
            flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            break;
        case Wrapping::ClampBorder:
            flags |= BGFX_SAMPLER_U_BORDER | BGFX_SAMPLER_V_BORDER;
            break;
        case Wrapping::Repeat:
            break;
        case Wrapping::MirroredRepeat:
            flags |= BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;
            break;
        }

        switch (filter)
        {
        case Filter::Nearest:
        case Filter::NearestMipmapNearest:
        case Filter::NearestMipmapLinear:
            flags |= BGFX_SAMPLER_POINT;
            break;
        case Filter::Linear:
        case Filter::LinearMipmapNearest:
        case Filter::LinearMipmapLinear:
            flags |= BGFX_SAMPLER_UVW_MIRROR;
            break;
        }

        handle = bgfx::createTexture2D(width, height, false, layers,
                                       getBgfxTextureFormat(format), flags, memory);

        bgfx::setName(handle, name.c_str());
        bgfx::frame();
        bgfx::calcTextureSize(info, width, height, 1, false, false, layers,
                              getBgfxTextureFormat(format));
        bgfx::frame();
    }

    void Texture2D::create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                           Wrapping wrapping, Filter filter, const void* data, uint32_t size) noexcept
    {
        create(width, height, layers, format, wrapping, filter, bgfx::makeRef(data, size));
    }

    void Texture2D::create(const void* data, uint32_t width, uint32_t height) {
        TINA_PROFILE_FUNCTION();
        
        m_Width = width;
        m_Height = height;

        // 如果已存在纹理,先销毁
        if (bgfx::isValid(handle)) {
            destroy();
        }

        // 创建纹理
        handle = bgfx::createTexture2D(
            uint16_t(width),
            uint16_t(height),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            data ? bgfx::copy(data, width * height * 4) : nullptr
        );

        if (!bgfx::isValid(handle)) {
            TINA_LOG_ERROR("Failed to create texture");
            throw std::runtime_error("Failed to create texture");
        }

        // 保存纹理数据用于重新加载
        if (data) {
            m_Data.resize(width * height * 4);
            memcpy(m_Data.data(), data, m_Data.size());
        }
    }

    void Texture2D::update(const void* data, uint32_t width, uint32_t height) {
        TINA_PROFILE_FUNCTION();
        
        if (!bgfx::isValid(handle)) {
            TINA_LOG_WARN("Texture handle invalid, creating new texture");
            create(data, width, height);
            return;
        }

        if (width != m_Width || height != m_Height) {
            TINA_LOG_INFO("Texture size changed, recreating texture");
            destroy();
            create(data, width, height);
            return;
        }

        // 更新纹理数据
        bgfx::updateTexture2D(
            handle,
            0,
            0,
            0,
            0,
            uint16_t(width),
            uint16_t(height),
            bgfx::copy(data, width * height * 4)
        );

        // 更新缓存的数据
        if (data) {
            m_Data.resize(width * height * 4);
            memcpy(m_Data.data(), data, m_Data.size());
        }
    }

    bool Texture2D::reload() {
        TINA_PROFILE_FUNCTION();
        
        if (m_Data.empty() || m_Path.empty()) {
            TINA_LOG_ERROR("Cannot reload texture: no data or path available");
            return false;
        }

        try {
            create(m_Data.data(), m_Width, m_Height);
            TINA_LOG_INFO("Successfully reloaded texture: {}", m_Path);
            return true;
        }
        catch (const std::exception& e) {
            TINA_LOG_ERROR("Failed to reload texture: {}", e.what());
            return false;
        }
    }

    void Texture2D::destroy() {
        TINA_PROFILE_FUNCTION();
        
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    void Texture2D::createTexture() {
        TINA_PROFILE_FUNCTION();
        
        // 销毁旧纹理
        destroy();

        // 创建新纹理
        const bgfx::Memory* mem = nullptr;
        if (!m_Data.empty()) {
            mem = bgfx::copy(m_Data.data(), m_Data.size());
        }

        uint64_t flags = BGFX_TEXTURE_NONE |
                         BGFX_SAMPLER_U_CLAMP |
                         BGFX_SAMPLER_V_CLAMP;

        handle = bgfx::createTexture2D(
            m_Width,
            m_Height,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            flags,
            mem
        );

        if (!bgfx::isValid(handle)) {
            throw std::runtime_error("Failed to create texture");
        }
    }
} // Tina
