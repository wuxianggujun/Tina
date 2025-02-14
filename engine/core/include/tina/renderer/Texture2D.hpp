//
// Created by wuxianggujun on 2025/2/11.
//

#pragma once

#include <string>
#include <memory>
#include "bgfx/bgfx.h"
#include <vector>
#include "tina/resources/ResourceManager.hpp"
#include "tina/utils/Profiler.hpp"

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

    class Texture2D : public Resource
    {
    public:
        explicit Texture2D(std::string name = "");
        ~Texture2D();

        // 禁止拷贝
        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        // 创建纹理的多个重载
        void create(const void* data, uint32_t width, uint32_t height);
        
        void create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                    Wrapping wrapping, Filter filter, const bgfx::Memory* memory) noexcept;
        
        void create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                    Wrapping wrapping, Filter filter, const void* data, uint32_t size) noexcept;
        
        // 更新纹理数据
        void update(const void* data, uint32_t width, uint32_t height);

        // Resource接口实现
        bool isValid() const override { return bgfx::isValid(handle); }
        bool reload() override;

        // Getters
        bgfx::TextureHandle getHandle() const { return handle; }
        bgfx::TextureHandle getNativeHandle() const { return handle; }
        uint32_t getWidth() const { return m_Width; }
        uint32_t getHeight() const { return m_Height; }
        const std::vector<uint8_t>& getData() const { return m_Data; }
        const std::string& getPath() const { return m_Path; }
        const std::string& getName() const { return name; }

        // Setters
        void setPath(const std::string& path) { m_Path = path; }
        void setName(const std::string& newName) { name = newName; }
        void setHandle(bgfx::TextureHandle h) { handle = h; }
        void setWidth(uint32_t width) { m_Width = width; }
        void setHeight(uint32_t height) { m_Height = height; }

    private:
        void destroy();
        void createTexture();

        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        bgfx::TextureInfo info;
        std::string name;
        
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::vector<uint8_t> m_Data;
        std::string m_Path;
    };
} // Tina
