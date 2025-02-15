//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/TextureManager.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/utils/BgfxUtils.hpp"

namespace Tina
{
    TextureManager::~TextureManager() = default;

    SharedPtr<Texture2D> TextureManager::loadTexture(const std::string& name, const std::string& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 检查纹理是否已加载
        const auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
            TINA_LOG_DEBUG("Texture '{}' already loaded, reusing existing texture", name);
            return it->second;
        }

        // 使用BgfxUtils加载纹理
        const auto result = Utils::BgfxUtils::loadTexture(path);
        if (!bgfx::isValid(result.handle))
        {
            TINA_LOG_ERROR("Failed to load texture:{}", path);
            return nullptr;
        }

        // 创建新的纹理对象
        auto texture = std::make_shared<Texture2D>(name);
        
        // 设置纹理数据
        texture->setHandle(result.handle);
        texture->setWidth(result.width);
        texture->setHeight(result.height);
        texture->setPath(path);

        if (texture->isValid())
        {
            m_textures[name] = texture;
            
            // 计算总内存使用
            size_t totalTextureMemory = 0;
            for (const auto& pair : m_textures)
            {
                const auto& tex = pair.second;
                totalTextureMemory += tex->getWidth() * tex->getHeight() * 4; // 假设RGBA8格式
            }
            
            TINA_LOG_INFO("Texture '{}' loaded successfully - Size: {}x{}, Estimated Memory: {} bytes, Total Texture Memory: {} bytes",
                name, result.width, result.height,
                result.width * result.height * 4,
                totalTextureMemory);
                
            return texture;
        }

        TINA_LOG_ERROR("Failed to create texture:{}", name);
        return nullptr;
    }

    SharedPtr<Texture2D> TextureManager::getTexture(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_textures.find(name);
        return (it != m_textures.end()) ? it->second : nullptr;
    }

    void TextureManager::releaseTexture(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
            const auto& texture = it->second;
            TINA_LOG_DEBUG("Releasing texture '{}' - Size: {}x{}, Estimated Memory: {} bytes",
                name, texture->getWidth(), texture->getHeight(),
                texture->getWidth() * texture->getHeight() * 4);
                
            m_textures.erase(it);
        }
    }

    void TextureManager::clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        size_t totalMemoryFreed = 0;
        for (const auto& pair : m_textures)
        {
            const auto& texture = pair.second;
            totalMemoryFreed += texture->getWidth() * texture->getHeight() * 4;
        }
        
        TINA_LOG_INFO("Clearing all textures - Total Memory Freed: {} bytes", totalMemoryFreed);
        m_textures.clear();
    }

    void TextureManager::shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (bgfx::getInternalData() && bgfx::getInternalData()->context)
        {
            size_t totalMemoryFreed = 0;
            for (const auto& pair : m_textures)
            {
                const auto& texture = pair.second;
                totalMemoryFreed += texture->getWidth() * texture->getHeight() * 4;
            }
            
            TINA_LOG_INFO("TextureManager shutdown - Total Memory Freed: {} bytes", totalMemoryFreed);
            m_textures.clear();
        }
    }
} // Tina
