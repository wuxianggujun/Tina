#pragma once

#include "tina/resource/ResourceLoader.hpp"
#include "tina/resource/TextureResource.hpp"
#include <filesystem>

namespace Tina
{
    /**
     * @brief 纹理加载器类
     */
    class TINA_CORE_API TextureLoader : public IResourceLoader {
    public:
        TINA_REGISTER_RESOURCE_LOADER(TextureLoader, TextureResource)

        bool loadSync(Resource* resource, 
            const ResourceLoadProgressCallback& progressCallback = nullptr) override;

        std::future<bool> loadAsync(Resource* resource,
            const ResourceLoadProgressCallback& progressCallback = nullptr) override;

        void unload(Resource* resource) override;

        bool validate(Resource* resource) override;

    private:
        /**
         * @brief 加载图片文件
         * @param filePath 文件路径
         * @param width 返回图片宽度
         * @param height 返回图片高度
         * @param channels 返回图片通道数
         * @return 图片数据
         */
        std::vector<uint8_t> loadImageFile(const std::filesystem::path& filePath,
            int* width, int* height, int* channels);

        /**
         * @brief 创建bgfx纹理
         * @param data 图片数据
         * @param width 图片宽度
         * @param height 图片高度
         * @param format 纹理格式
         * @return 纹理句柄
         */
        bgfx::TextureHandle createTexture(const void* data, uint16_t width, uint16_t height,
            bgfx::TextureFormat::Enum format);

        /**
         * @brief 根据图片通道数确定纹理格式
         * @param channels 图片通道数
         * @return 纹理格式
         */
        bgfx::TextureFormat::Enum getTextureFormat(int channels);
    };
}