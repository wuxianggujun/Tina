//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/TextureManager.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/utils/BgfxUtils.hpp"

namespace Tina
{
    TextureManager& TextureManager::getInstance()
    {
        static TextureManager instance;
        return instance;
    }

    SharedPtr<Texture2D> TextureManager::loadTexture(const std::string& name, const std::string& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 检查纹理是否已加载
        const auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
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
            TINA_LOG_INFO("Texture '{}' loaded successfully", name);
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
            m_textures.erase(it);
        }
    }

    void TextureManager::clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_textures.clear();
    }

    TextureManager::~TextureManager()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 在销毁纹理之前检查BGFX Context
        if (bgfx::getInternalData() && bgfx::getInternalData()->context)
        {
            m_textures.clear(); // 这会触发Texture2D的析构函数
        }
    }
} // Tina
