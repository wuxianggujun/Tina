//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/TextureManager.hpp"
#include "tina/resources/ResourceManager.hpp"
#include "tina/resources/TextureLoader.hpp"
#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/utils/BgfxUtils.hpp"
#include <filesystem>

namespace Tina
{
    TextureManager::TextureManager() = default;
    TextureManager::~TextureManager() = default;

    SharedPtr<Texture2D> TextureManager::loadTexture(const std::string& name, const std::string& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 检查纹理是否已加载
        const auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
            TINA_CORE_LOG_DEBUG("Texture '{}' already loaded, reusing existing texture", name);
            return it->second;
        }

        // 构建完整的资源路径
        std::filesystem::path resourcePath = std::filesystem::current_path() / "resources" / path;

        // 检查目录和文件是否存在
        if (!std::filesystem::exists(resourcePath.parent_path()))
        {
            TINA_CORE_LOG_ERROR("Resource directory does not exist: {}", resourcePath.parent_path().string());
            return nullptr;
        }

        if (!std::filesystem::exists(resourcePath))
        {
            TINA_CORE_LOG_ERROR("Texture file does not exist: {}", resourcePath.string());
            return nullptr;
        }

        TINA_CORE_LOG_DEBUG("Loading texture from path: {}", resourcePath.string());

        // 使用 ResourceManager 加载纹理
        auto texture = ResourceManager::getInstance().load<Texture2D>(resourcePath.string());
        if (!texture)
        {
            TINA_CORE_LOG_ERROR("Failed to load texture through ResourceManager: {}", resourcePath.string());
            return nullptr;
        }

        // 验证纹理是否有效
        if (!texture->isValid() || texture->getWidth() == 0 || texture->getHeight() == 0)
        {
            TINA_CORE_LOG_ERROR("Loaded texture is invalid: {}", resourcePath.string());
            return nullptr;
        }

        // 缓存纹理
        m_textures[name] = texture;

        // 计算总内存使用
        size_t totalTextureMemory = 0;
        for (const auto& pair : m_textures)
        {
            const auto& tex = pair.second;
            if (tex && tex->isValid())
            {
                totalTextureMemory += tex->getWidth() * tex->getHeight() * 4;
            }
        }

        TINA_CORE_LOG_INFO("Texture '{}' loaded successfully - Path: {}, Size: {}x{}, Memory: {} KB, Total Memory: {} KB",
                      name,
                      resourcePath.string(),
                      texture->getWidth(),
                      texture->getHeight(),
                      (texture->getWidth() * texture->getHeight() * 4) / 1024,
                      totalTextureMemory / 1024);

        return texture;
    }

    SharedPtr<Texture2D> TextureManager::getTexture(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_textures.find(name);
        if (it != m_textures.end() && it->second && it->second->isValid())
        {
            return it->second;
        }
        TINA_CORE_LOG_WARN("Texture '{}' not found or invalid", name);
        return nullptr;
    }

    void TextureManager::releaseTexture(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
            const auto& texture = it->second;
            if (texture && texture->isValid())
            {
                TINA_CORE_LOG_DEBUG("Releasing texture '{}' - Size: {}x{}, Memory: {} KB",
                               name,
                               texture->getWidth(),
                               texture->getHeight(),
                               (texture->getWidth() * texture->getHeight() * 4) / 1024);

                // 从 ResourceManager 中释放
                ResourceManager::getInstance().release<Texture2D>(texture->getPath());
            }
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
            if (texture && texture->isValid())
            {
                totalMemoryFreed += texture->getWidth() * texture->getHeight() * 4;
                // 从 ResourceManager 中释放
                ResourceManager::getInstance().release<Texture2D>(texture->getPath());
            }
        }

        TINA_CORE_LOG_INFO("Cleared all textures - Total Memory Freed: {} KB", totalMemoryFreed / 1024);
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
                if (texture && texture->isValid())
                {
                    totalMemoryFreed += texture->getWidth() * texture->getHeight() * 4;
                    // 从 ResourceManager 中释放
                    ResourceManager::getInstance().release<Texture2D>(texture->getPath());
                }
            }

            TINA_CORE_LOG_INFO("TextureManager shutdown - Total Memory Freed: {} KB", totalMemoryFreed / 1024);
            m_textures.clear();
        }
    }
} // Tina
