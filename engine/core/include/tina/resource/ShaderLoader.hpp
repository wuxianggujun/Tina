#pragma once

#include "tina/resource/ResourceLoader.hpp"
#include "tina/resource/ShaderResource.hpp"
#include <filesystem>

namespace Tina {

/**
 * @brief 着色器加载器类
 * 
 * 负责加载和编译着色器资源。
 * 支持从二进制文件加载预编译的着色器。
 */
class TINA_CORE_API ShaderLoader : public IResourceLoader {
public:
    TINA_REGISTER_RESOURCE_LOADER(ShaderLoader, ShaderResource)

    ShaderLoader() = default;
    ~ShaderLoader() = default;

    bool loadSync(Resource* resource, 
        const ResourceLoadProgressCallback& progressCallback = nullptr) override;

    std::future<bool> loadAsync(Resource* resource,
        const ResourceLoadProgressCallback& progressCallback = nullptr) override;

    void unload(Resource* resource) override;

    bool validate(Resource* resource) override;

private:
    /**
     * @brief 从二进制文件加载着色器
     * @param filePath 文件路径
     * @return 着色器数据
     */
    std::vector<uint8_t> loadShaderBinary(const std::filesystem::path& filePath);

    /**
     * @brief 创建着色器程序
     * @param vsData 顶点着色器数据
     * @param fsData 片段着色器数据
     * @return 着色器程序句柄
     */
    bgfx::ProgramHandle createShaderProgram(
        const std::vector<uint8_t>& vsData,
        const std::vector<uint8_t>& fsData);
};

} // namespace Tina 