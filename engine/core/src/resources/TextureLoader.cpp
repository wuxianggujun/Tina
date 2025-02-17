#include "tina/resources/TextureLoader.hpp"
#include "tina/log/Logger.hpp"
#include "tina/utils/Profiler.hpp"
#include <tina/utils/stb_image.h>

namespace Tina {

std::shared_ptr<Resource> TextureLoader::load(const std::string& path) {
    TINA_PROFILE_FUNCTION();
    
    try {
        auto texture = std::make_shared<Texture2D>();
        if (loadTextureData(path, texture.get())) {
            texture->setPath(path);
            TINA_LOG_INFO("Successfully loaded texture: {}", path);
            return texture;
        }
        TINA_LOG_ERROR("Failed to load texture: {}", path);
        return nullptr;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Exception while loading texture {}: {}", path, e.what());
        return nullptr;
    }
}

bool TextureLoader::reload(Resource* resource) {
    TINA_PROFILE_FUNCTION();
    
    auto texture = dynamic_cast<Texture2D*>(resource);
    if (!texture) {
        TINA_LOG_ERROR("Invalid resource type for texture reload");
        return false;
    }

    try {
        // 获取纹理的原始路径
        const auto& path = texture->getPath();
        if (path.empty()) {
            TINA_LOG_ERROR("Texture has no associated path");
            return false;
        }

        // 重新加载纹理数据
        if (!loadTextureData(path, texture)) {
            TINA_LOG_ERROR("Failed to reload texture data: {}", path);
            return false;
        }
        
        TINA_LOG_INFO("Successfully reloaded texture: {}", path);
        return true;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Exception while reloading texture: {}", e.what());
        return false;
    }
}

bool TextureLoader::loadTextureData(const std::string& path, Texture2D* texture) {
    TINA_PROFILE_FUNCTION();

    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    
    // 加载图像数据
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        TINA_LOG_ERROR("Failed to load image: {}", path);
        return false;
    }

    try {
        // 将数据存储到纹理对象
        size_t dataSize = width * height * 4; // RGBA = 4 channels
        std::vector<uint8_t> textureData(data, data + dataSize);
        texture->setData(std::move(textureData));
        
        // 设置纹理尺寸
        texture->setWidth(width);
        texture->setHeight(height);
        
        // 创建 BGFX 纹理
        texture->create(data, width, height);
        
        // 验证纹理是否创建成功
        if (!texture->isValid()) {
            TINA_LOG_ERROR("Failed to create BGFX texture: {}", path);
            stbi_image_free(data);
            return false;
        }
        
        TINA_LOG_DEBUG("Created BGFX texture: {}x{}, Handle: {}", width, height, texture->getHandle().idx);
        
        // 释放stb_image分配的内存
        stbi_image_free(data);
        
        return true;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Exception while creating texture: {}", e.what());
        stbi_image_free(data);
        return false;
    }
}

} // namespace Tina