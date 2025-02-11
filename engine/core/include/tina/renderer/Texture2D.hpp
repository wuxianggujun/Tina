//
// Created by wuxianggujun on 2025/2/11.
//

#pragma once

#include <string>

#include "bgfx/bgfx.h"

namespace Tina
{
    enum class Format : uint8_t
    {
        BlockCompression1,
        BlockCompression2,
        BlockCompression3,
        Depth24Stencil8,
        DepthComponent16,
        DepthComponent24,
        R16F,
        R16I,
        R16SNorm,
        R16UI,
        R32F,
        R32I,
        R32UI,
        A8,
        R8,
        R8I,
        R8SNorm,
        R8UI,
        RG16,
        RG16F,
        RG16SNorm,
        RG32F,
        RG32I,
        RG32UI,
        RG8,
        RG8I,
        RG8SNorm,
        RG8UI,
        RGB10A2,
        B5G6R5,
        R5G6B5,
        BGR5A1,
        RGB5A1,
        RGB8,
        RGB8I,
        RGB8UI,
        RGB9E5,
        RGBA8,
        RGBA8I,
        RGBA8UI,
        RGBA8SNorm,
        BGRA8,
        RGBA16,
        RGBA16F,
        RGBA16I,
        RGBA16UI,
        RGBA16SNorm,
        RGBA32F,
        RGBA32I,
        RGBA32UI,
        BGRA4,
        RGBA4,
    };

    enum class Filter : uint8_t
    {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear,
    };

    enum class Wrapping : uint8_t
    {
        ClampEdge,
        ClampBorder,
        Repeat,
        MirroredRepeat,
    };

    class Texture2D
    {
    public:
        explicit Texture2D(std::string name);
        ~Texture2D();

        Texture2D(const Texture2D& texture) = delete;
        Texture2D& operator=(const Texture2D& texture) = delete;


        void create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                    Wrapping wrapping, Filter filter, const bgfx::Memory* memory) noexcept;

        void create(uint16_t width, uint16_t height, uint16_t layers, Format format = Format::RGBA8,
                    Wrapping wrapping = Wrapping::ClampEdge, Filter filter = Filter::Linear,
                    const void* data = nullptr, uint32_t size = 0) noexcept;

        [[nodiscard]] const std::string& getName() const { return name; }
        [[nodiscard]] const bgfx::TextureHandle& getNativeHandle() const { return handle; }
        [[nodiscard]] uint16_t getWidth() const { return info.width; }
        [[nodiscard]] uint16_t getHeight() const { return info.height; }
        [[nodiscard]] uint16_t getLayerCount() const { return info.numLayers; }
        [[nodiscard]] bgfx::TextureFormat::Enum getFormat() const { return info.format; }
        [[nodiscard]] bool isValid() const { return bgfx::isValid(handle); }

    protected:
        std::string name;
        bgfx::TextureHandle handle;
        bgfx::TextureInfo info;
    };
} // Tina
