#include "tina/resource/TextureLoader.hpp"
#include "tina/log/Log.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include <tina/utils/stb_image.h>
#include <bimg/bimg.h>

namespace Tina {

bool TextureLoader::loadSync(Resource* resource, const ResourceLoadProgressCallback& progressCallback) {
    try {
        auto* textureResource = dynamic_cast<TextureResource*>(resource);
        if (!textureResource) {
            TINA_ENGINE_ERROR("Invalid resource type");
            return false;
        }

        TINA_ENGINE_DEBUG("Starting texture load for '{}', current refCount: {}", 
            resource->getName(), textureResource->getRefCount());

        // 设置初始状态
        textureResource->m_state = ResourceState::Loading;
        textureResource->m_handle = BGFX_INVALID_HANDLE;

        if (progressCallback) {
            progressCallback(0.0f, "开始加载纹理");
        }

        // 加载图片文件
        int width, height, channels;
        auto imagePath = std::filesystem::path(resource->getPath()) / resource->getName();
        
        TINA_ENGINE_DEBUG("Loading image from: {}", imagePath.string());
        
        auto imageData = loadImageFile(imagePath, &width, &height, &channels);
        if (imageData.empty()) {
            TINA_ENGINE_ERROR("Failed to load image file: {}", imagePath.string());
            textureResource->m_state = ResourceState::Failed;
            return false;
        }

        if (progressCallback) {
            progressCallback(0.5f, "创建纹理");
        }

        // 创建纹理
        auto format = getTextureFormat(channels);
        auto handle = createTexture(imageData.data(), width, height, format);
        if (!bgfx::isValid(handle)) {
            TINA_ENGINE_ERROR("Failed to create texture");
            textureResource->m_state = ResourceState::Failed;
            return false;
        }

        // 更新资源状态
        textureResource->m_handle = handle;
        textureResource->m_width = width;
        textureResource->m_height = height;
        textureResource->m_format = format;
        textureResource->m_state = ResourceState::Loaded;

        // 更新资源大小
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
        auto* textureResource = dynamic_cast<TextureResource*>(resource);
        if (textureResource) {
            textureResource->m_state = ResourceState::Failed;
        }
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
    auto* textureResource = dynamic_cast<TextureResource*>(resource);
    if (textureResource) {
        textureResource->unload();
    }
}

bool TextureLoader::validate(Resource* resource) {
    auto* textureResource = dynamic_cast<TextureResource*>(resource);
    return textureResource && textureResource->isLoaded();
}

std::vector<uint8_t> TextureLoader::loadImageFile(const std::filesystem::path& filePath,
    int* width, int* height, int* channels) {
    
    // 使用stb_image加载图片
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.string().c_str(), width, height, channels, 0);
    
    if (!data) {
        TINA_ENGINE_ERROR("Failed to load image: {}", stbi_failure_reason());
        return {};
    }

    // 将数据复制到vector中
    size_t dataSize = (*width) * (*height) * (*channels);
    std::vector<uint8_t> buffer(data, data + dataSize);
    
    // 释放stb_image分配的内存
    stbi_image_free(data);
    
    return buffer;
}

bgfx::TextureHandle TextureLoader::createTexture(const void* data, uint16_t width, uint16_t height,
    bgfx::TextureFormat::Enum format) {
    
    const bgfx::Memory* mem = bgfx::copy(data, width * height * bimg::getBitsPerPixel(bimg::TextureFormat::Enum(format)) / 8);
    
    return bgfx::createTexture2D(
        width,
        height,
        false,
        1,
        format,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        mem
    );
}

bgfx::TextureFormat::Enum TextureLoader::getTextureFormat(int channels) {
    switch (channels) {
        case 1:
            return bgfx::TextureFormat::R8;
        case 2:
            return bgfx::TextureFormat::RG8;
        case 3:
            return bgfx::TextureFormat::RGB8;
        case 4:
            return bgfx::TextureFormat::RGBA8;
        default:
            TINA_ENGINE_ERROR("Unsupported number of channels: {}", channels);
            return bgfx::TextureFormat::Unknown;
    }
}

} // namespace Tina 