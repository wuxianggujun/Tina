#include "tina/resource/TextureLoader.hpp"
#include "tina/log/Log.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "tina/utils/stb_image.h"
#include <fmt/format.h>

namespace Tina {

bool TextureLoader::loadSync(Resource* resource, const ResourceLoadProgressCallback& progressCallback) {
    if (!resource) {
        TINA_ENGINE_ERROR("Invalid resource pointer");
        return false;
    }

    auto* textureResource = dynamic_cast<TextureResource*>(resource);
    if (!textureResource) {
        TINA_ENGINE_ERROR("Resource is not a TextureResource");
        return false;
    }

    try {
        // 更新资源状态
        updateResourceState(resource, ResourceState::Loading);
        if (progressCallback) {
            progressCallback(0.0f, "开始加载纹理");
        }

        // 加载图片数据
        if (progressCallback) {
            progressCallback(0.2f, "加载图片数据");
        }

        int width, height, channels;
        auto imageData = loadImageData(resource->getPath(), &width, &height, &channels);
        if (imageData.empty()) {
            updateResourceState(resource, ResourceState::Failed);
            return false;
        }

        // 创建纹理
        if (progressCallback) {
            progressCallback(0.6f, "创建纹理");
        }

        bgfx::TextureHandle handle = createTexture(
            imageData,
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            textureResource->getConfig());

        if (!bgfx::isValid(handle)) {
            updateResourceState(resource, ResourceState::Failed);
            return false;
        }

        // 更新资源
        if (progressCallback) {
            progressCallback(0.8f, "更新资源状态");
        }

        textureResource->m_handle = handle;
        textureResource->m_width = static_cast<uint16_t>(width);
        textureResource->m_height = static_cast<uint16_t>(height);
        updateResourceState(resource, ResourceState::Loaded);
        updateResourceSize(resource, imageData.size());

        if (progressCallback) {
            progressCallback(1.0f, "纹理加载完成");
        }

        TINA_ENGINE_INFO("Successfully loaded texture '{}' ({}x{}, {} channels)", 
            resource->getName(), width, height, channels);
        return true;
    }
    catch (const std::exception& e) {
        TINA_ENGINE_ERROR("Failed to load texture '{}': {}", resource->getName(), e.what());
        updateResourceState(resource, ResourceState::Failed);
        return false;
    }
}

std::future<bool> TextureLoader::loadAsync(Resource* resource, 
    const ResourceLoadProgressCallback& progressCallback) {
    return std::async(std::launch::async, [this, resource, progressCallback]() {
        return loadSync(resource, progressCallback);
    });
}

void TextureLoader::unload(Resource* resource) {
    if (!resource) return;

    auto* textureResource = dynamic_cast<TextureResource*>(resource);
    if (textureResource) {
        textureResource->unload();
    }
}

bool TextureLoader::validate(Resource* resource) {
    if (!resource) {
        TINA_ENGINE_ERROR("Invalid resource pointer");
        return false;
    }

    auto* textureResource = dynamic_cast<TextureResource*>(resource);
    if (!textureResource) {
        TINA_ENGINE_ERROR("Resource is not a TextureResource");
        return false;
    }

    return bgfx::isValid(textureResource->getHandle());
}

std::vector<uint8_t> TextureLoader::loadImageData(
    const std::string& filePath,
    int* width,
    int* height,
    int* channels) {
    // 设置stb_image选项
    stbi_set_flip_vertically_on_load(true);

    // 加载图片
    int w, h, c;
    uint8_t* data = stbi_load(filePath.c_str(), &w, &h, &c, STBI_rgb_alpha);
    if (!data) {
        TINA_ENGINE_ERROR("Failed to load image: {}", stbi_failure_reason());
        return {};
    }

    // 复制数据
    std::vector<uint8_t> result(data, data + w * h * 4);
    stbi_image_free(data);

    // 设置输出参数
    if (width) *width = w;
    if (height) *height = h;
    if (channels) *channels = 4; // 我们总是转换为RGBA

    return result;
}

bgfx::TextureHandle TextureLoader::createTexture(
    const std::vector<uint8_t>& data,
    uint16_t width,
    uint16_t height,
    const TextureSamplerConfig& config) {
    // 创建纹理
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        width,
        height,
        config.hasMips,
        config.numLayers,
        config.format,
        config.flags,
        bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));

    if (!bgfx::isValid(handle)) {
        TINA_ENGINE_ERROR("Failed to create texture");
        return BGFX_INVALID_HANDLE;
    }

    return handle;
}

} // namespace Tina 