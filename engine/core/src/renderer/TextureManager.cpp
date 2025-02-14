//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/TextureManager.hpp"
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

        auto texture = Texture2D::create(name);
        texture->setNativeHandle(result.handle, result.width, result.height, result.depth, result.hasMips,
                                 result.layers, result.format);

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
        const auto it = m_textures.find(name);
        return (it != m_textures.end()) ? it->second : nullptr;
    }

    void TextureManager::releaseTexture(const std::string& name)
    {
        auto it = m_textures.find(name);
        if (it != m_textures.end())
        {
            m_textures.erase(it);
        }
    }

    void TextureManager::clear()
    {
        m_textures.clear();
    }
} // Tina
