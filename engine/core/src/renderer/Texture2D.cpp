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
#include "bgfx/defines.h"

namespace Tina
{
    constexpr std::array<bgfx::TextureFormat::Enum, static_cast<size_t>(Texture2D::Format::Count)> k_TextureFormatsBgfx{
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
        bgfx::TextureFormat::RG8S, // R8SNorm
        bgfx::TextureFormat::RG8U, // RG8UI
        bgfx::TextureFormat::RGB10A2, // RGB10A2
        bgfx::TextureFormat::B5G6R5, // B5G6R5
        bgfx::TextureFormat::R5G6B5, // R5G6B5
        bgfx::TextureFormat::BGR5A1, // BGRA1
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

    static bgfx::TextureFormat::Enum getBgfxTextureFormat(Texture2D::Format format)
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
        try {
            destroy();
            TINA_LOG_DEBUG("Texture2D destroyed: {}", name);
        }
        catch (const std::exception& e) {
            TINA_LOG_ERROR("Error in Texture2D destructor: {}", e.what());
        }
    }

    bool Texture2D::reload() {
        TINA_PROFILE_FUNCTION();
        if (m_Path.empty()) {
            return false;
        }
        
        // 重新加载纹理数据
        destroy();
        
        // 使用 TextureLoader 重新加载
        return ResourceManager::getInstance().load<Texture2D>(m_Path) != nullptr;
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
            flags |= BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;  // BGFX 使用 MIRROR 作为重复模式
            break;
        case Wrapping::MirroredRepeat:
            flags |= BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;
            break;
        }

        switch (filter)
        {
        case Filter::Nearest:
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
            break;
        case Filter::Linear:
            flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
            break;
        case Filter::NearestMipmapNearest:
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            break;
        case Filter::LinearMipmapNearest:
            flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC | BGFX_SAMPLER_MIP_POINT;
            break;
        case Filter::NearestMipmapLinear:
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;  // BGFX 不支持 MIP_LINEAR
            break;
        case Filter::LinearMipmapLinear:
            flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC | BGFX_SAMPLER_MIP_POINT;  // BGFX 不支持 MIP_LINEAR
            break;
        }

        info.width = width;
        info.height = height;
        info.layers = layers;
        info.format = format;
        info.wrapping = wrapping;
        info.filter = filter;

        handle = bgfx::createTexture2D(
            width,
            height,
            false,
            layers,
            getBgfxTextureFormat(format),
            flags,
            memory
        );
    }

    void Texture2D::create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                           Wrapping wrapping, Filter filter, const void* data, uint32_t size) {
        const bgfx::Memory* mem = bgfx::copy(data, size);
        create(width, height, layers, format, wrapping, filter, mem);
    }

    void Texture2D::create(const void* data, uint32_t width, uint32_t height) {
        const bgfx::Memory* mem = bgfx::copy(data, width * height * 4);
        create(width, height, 1, Format::RGBA8, Wrapping::Repeat, Filter::Linear, mem);
    }

    void Texture2D::update(const void* data, uint32_t width, uint32_t height) {
        TINA_PROFILE_FUNCTION();
        if (!isValid()) {
            TINA_LOG_ERROR("Cannot update invalid texture");
            return;
        }

        if (width != info.width || height != info.height) {
            TINA_LOG_ERROR("Update dimensions do not match texture dimensions");
            return;
        }

        bgfx::updateTexture2D(
            handle,
            0,
            0,
            0,
            0,
            width,
            height,
            bgfx::copy(data, width * height * 4)
        );
    }

    void Texture2D::destroy() {
        TINA_PROFILE_FUNCTION();
        if (isValid()) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

} // Tina
