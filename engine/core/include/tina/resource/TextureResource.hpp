#pragma once

#include "tina/resource/Resource.hpp"
#include "tina/core/RefPtr.hpp"
#include <bgfx/bgfx.h>

namespace Tina
{
    /**
     * @brief 纹理资源类
     */
    class TINA_CORE_API TextureResource : public Resource {
    public:
        TINA_REGISTER_RESOURCE_TYPE(TextureResource)

        /**
         * @brief 创建纹理资源
         * @param name 资源名称
         * @param path 资源路径
         * @return 纹理资源指针
         */
        static RefPtr<TextureResource> create(const std::string& name, const std::string& path);

        ~TextureResource() override;

        /**
         * @brief 检查纹理是否已加载
         */
        bool isLoaded() const override;

        /**
         * @brief 获取纹理句柄
         */
        bgfx::TextureHandle getHandle() const { return m_handle; }

        /**
         * @brief 获取纹理宽度
         */
        uint16_t getWidth() const { return m_width; }

        /**
         * @brief 获取纹理高度
         */
        uint16_t getHeight() const { return m_height; }

        /**
         * @brief 获取纹理格式
         */
        bgfx::TextureFormat::Enum getFormat() const { return m_format; }

        /**
         * @brief 设置采样器状态
         */
        void setSamplerFlags(uint32_t flags);

    protected:
        TextureResource(const std::string& name, const std::string& path);

        bool load() override;
        void unload() override;

    private:
        bgfx::TextureHandle m_handle{BGFX_INVALID_HANDLE};  // 纹理句柄
        uint16_t m_width{0};                                // 纹理宽度
        uint16_t m_height{0};                               // 纹理高度
        bgfx::TextureFormat::Enum m_format{bgfx::TextureFormat::Unknown}; // 纹理格式
        uint32_t m_flags{BGFX_TEXTURE_NONE};               // 采样器标志

        friend class TextureLoader;
    };
}