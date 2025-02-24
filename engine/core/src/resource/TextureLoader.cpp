#include "tina/resource/TextureLoader.hpp"
#include "tina/log/Log.hpp"
// #define STB_IMAGE_IMPLEMENTATION
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
    
    // 使用stb_image加载图片，强制使用RGBA格式
    stbi_set_flip_vertically_on_load(false);  // 不翻转图像，因为我们使用DirectX风格的UV坐标
    unsigned char* data = stbi_load(filePath.string().c_str(), width, height, channels, 4);  // 强制4通道
    
    if (!data) {
        TINA_ENGINE_ERROR("Failed to load image: {}", stbi_failure_reason());
        return {};
    }

    // 将数据复制到vector中
    size_t dataSize = (*width) * (*height) * 4;  // 总是使用4通道
    std::vector<uint8_t> buffer(data, data + dataSize);
    
    // 释放stb_image分配的内存
    stbi_image_free(data);
    
    *channels = 4;  // 更新通道数为4
    return buffer;
}

bgfx::TextureHandle TextureLoader::createTexture(const void* data, uint16_t width, uint16_t height,
    bgfx::TextureFormat::Enum format) {
    
    // 计算每像素字节数
    uint32_t bpp = bimg::getBitsPerPixel(bimg::TextureFormat::Enum(format)) / 8;
    uint32_t pitch = width * bpp;
    uint32_t size = pitch * height;
    
    TINA_ENGINE_DEBUG("Creating texture - Size: {}x{}, Format: {}, BPP: {}, Total size: {}", 
        width, height, (int)format, bpp, size);
    
    // 检查数据是否有效
    if (!data) {
        TINA_ENGINE_ERROR("Texture data is null");
        return BGFX_INVALID_HANDLE;
    }
    
    // 创建纹理内存
    const bgfx::Memory* mem = bgfx::copy(data, size);
    
    // 设置纹理标志
    uint64_t flags = BGFX_SAMPLER_U_CLAMP 
                   | BGFX_SAMPLER_V_CLAMP
                   | BGFX_SAMPLER_MIN_POINT
                   | BGFX_SAMPLER_MAG_POINT;
    
    // 创建纹理
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        width,
        height,
        false,  // 不使用mipmap
        1,      // 层数
        format,
        flags,
        mem
    );
    
    if (!bgfx::isValid(handle)) {
        TINA_ENGINE_ERROR("Failed to create texture");
        return BGFX_INVALID_HANDLE;
    }
    
    TINA_ENGINE_INFO("Texture created successfully - Handle: {}, Format: {}, Flags: {:#x}", 
        handle.idx, (int)format, flags);
    return handle;
}

bgfx::TextureFormat::Enum TextureLoader::getTextureFormat(int channels) {
    // 始终返回RGBA8格式
    return bgfx::TextureFormat::RGBA8;
}

} // namespace Tina 