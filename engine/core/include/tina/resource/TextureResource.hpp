#pragma once

#include "tina/resource/Resource.hpp"
#include <bgfx/bgfx.h>

namespace Tina {

/**
 * @brief 纹理采样器配置
 */
struct TextureSamplerConfig {
    uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;  // 采样器标志
    bool hasMips = false;                                           // 是否生成mipmap
    uint8_t numLayers = 1;                                         // 纹理层数
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8; // 纹理格式
};

/**
 * @brief 纹理资源类
 * 
 * 管理bgfx纹理和相关资源。
 * 支持纹理采样器配置和mipmap生成。
 */
class TINA_CORE_API TextureResource : public Resource {
public:
    TINA_REGISTER_RESOURCE_TYPE(TextureResource)

    /**
     * @brief 创建纹理资源
     * @param name 资源名称
     * @param path 资源路径
     * @param config 采样器配置
     * @return 纹理资源指针
     */
    static RefPtr<TextureResource> create(
        const std::string& name, 
        const std::string& path,
        const TextureSamplerConfig& config = TextureSamplerConfig()) {
        return RefPtr<TextureResource>(new TextureResource(name, path, config));
    }

    /**
     * @brief 获取纹理句柄
     * @return 纹理句柄
     */
    bgfx::TextureHandle getHandle() const { return m_handle; }

    /**
     * @brief 获取纹理宽度
     * @return 纹理宽度
     */
    uint16_t getWidth() const { return m_width; }

    /**
     * @brief 获取纹理高度
     * @return 纹理高度
     */
    uint16_t getHeight() const { return m_height; }

    /**
     * @brief 获取纹理格式
     * @return 纹理格式
     */
    bgfx::TextureFormat::Enum getFormat() const { return m_config.format; }

    /**
     * @brief 获取采样器配置
     * @return 采样器配置
     */
    const TextureSamplerConfig& getConfig() const { return m_config; }

protected:
    TextureResource(
        const std::string& name, 
        const std::string& path,
        const TextureSamplerConfig& config);
    ~TextureResource();

    bool load() override;
    void unload() override;

private:
    bgfx::TextureHandle m_handle{BGFX_INVALID_HANDLE}; // 纹理句柄
    TextureSamplerConfig m_config;                     // 采样器配置
    uint16_t m_width{0};                              // 纹理宽度
    uint16_t m_height{0};                             // 纹理高度

    friend class TextureLoader;
};

} // namespace Tina 