#pragma once

#include "tina/resource/ResourceLoader.hpp"
#include "tina/resource/TextureResource.hpp"

namespace Tina {

/**
 * @brief 纹理加载器类
 * 
 * 负责加载和创建纹理资源。
 * 支持多种图片格式和纹理配置。
 */
class TINA_CORE_API TextureLoader : public IResourceLoader {
public:
    TINA_REGISTER_RESOURCE_LOADER(TextureLoader, TextureResource)

    TextureLoader() = default;
    ~TextureLoader() = default;

    bool loadSync(Resource* resource, 
        const ResourceLoadProgressCallback& progressCallback = nullptr) override;

    std::future<bool> loadAsync(Resource* resource,
        const ResourceLoadProgressCallback& progressCallback = nullptr) override;

    void unload(Resource* resource) override;

    bool validate(Resource* resource) override;

private:
    /**
     * @brief 加载图片数据
     * @param filePath 文件路径
     * @param width 输出图片宽度
     * @param height 输出图片高度
     * @param channels 输出图片通道数
     * @return 图片数据
     */
    std::vector<uint8_t> loadImageData(
        const std::string& filePath,
        int* width,
        int* height,
        int* channels);

    /**
     * @brief 创建纹理
     * @param data 图片数据
     * @param width 图片宽度
     * @param height 图片高度
     * @param config 纹理配置
     * @return 纹理句柄
     */
    bgfx::TextureHandle createTexture(
        const std::vector<uint8_t>& data,
        uint16_t width,
        uint16_t height,
        const TextureSamplerConfig& config);
};

} // namespace Tina 