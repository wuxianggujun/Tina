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

        // 创建临时纹理
        auto tempTexture = std::make_shared<Texture2D>();
        if (!loadTextureData(path, tempTexture.get())) {
            TINA_LOG_ERROR("Failed to reload texture data: {}", path);
            return false;
        }

        // 更新原始纹理 - 使用data()获取vector的底层指针
        const auto& textureData = tempTexture->getData();
        texture->update(textureData.data(), tempTexture->getWidth(), tempTexture->getHeight());
        
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
    
    // 加载图像数据
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        TINA_LOG_ERROR("Failed to load image data: {}", stbi_failure_reason());
        return false;
    }

    // 创建纹理
    try {
        texture->create(data, width, height);
        texture->setPath(path);
        stbi_image_free(data);
        return true;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to create texture: {}", e.what());
        stbi_image_free(data);
        return false;
    }
}

} // namespace Tina 