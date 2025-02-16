#pragma once

#include <memory>
#include <string>
#include <vector>
#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/bx.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/pixelformat.h>
#include "tina/core/Core.hpp"
#include "tina/utils/Profiler.hpp"
#include "tina/resources/Resource.hpp"
#include "tina/resources/ResourceManager.hpp"

namespace Tina {

class TINA_CORE_API Texture2D : public Resource {
public:
    // 纹理格式
    enum class Format : uint8_t {
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
        BGRA1,
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
        Count
    };

    // 纹理包装模式
    enum class Wrapping : uint8_t {
        ClampEdge,
        ClampBorder,
        Repeat,
        MirroredRepeat
    };

    // 纹理过滤模式
    enum class Filter : uint8_t {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear
    };

    explicit Texture2D(std::string name = "");
    ~Texture2D() override;

    // 禁止拷贝
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    // Resource 接口实现
    bool isValid() const override { return bgfx::isValid(handle); }
    bool reload() override;
    void release() override { destroy(); }

    // 创建纹理的多个重载
    void create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                Wrapping wrapping, Filter filter, const bgfx::Memory* memory) noexcept;

    void create(uint16_t width, uint16_t height, uint16_t layers, Format format,
                Wrapping wrapping, Filter filter, const void* data, uint32_t size);

    void create(const void* data, uint32_t width, uint32_t height);

    // 更新纹理数据
    void update(const void* data, uint32_t width, uint32_t height);

    // 销毁纹理
    void destroy();

    // Getters
    const std::string& getName() const { return name; }
    uint32_t getWidth() const { return info.width; }
    uint32_t getHeight() const { return info.height; }
    bgfx::TextureHandle getHandle() const { return handle; }
    const std::string& getPath() const { return m_Path; }
    const std::vector<uint8_t>& getData() const { return m_Data; }

    // Setters
    void setHandle(bgfx::TextureHandle h) { handle = h; }
    void setWidth(uint32_t w) { info.width = w; }
    void setHeight(uint32_t h) { info.height = h; }
    void setPath(const std::string& path) { m_Path = path; }
    void setData(std::vector<uint8_t>&& data) { m_Data = std::move(data); }

private:
    void createTexture();

private:
    std::string name;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    struct {
        uint32_t width = 0;
        uint32_t height = 0;
        uint16_t layers = 1;
        Format format = Format::RGBA8;
        Wrapping wrapping = Wrapping::Repeat;
        Filter filter = Filter::Linear;
    } info;

    std::string m_Path;
    std::vector<uint8_t> m_Data; // 存储原始纹理数据
};

} // namespace Tina
