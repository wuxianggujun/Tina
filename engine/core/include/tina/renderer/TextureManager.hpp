//
// Created by wuxianggujun on 2025/2/14.
//

#pragma once

#include "Texture2D.hpp"
#include "tina/core/Core.hpp"

namespace Tina
{
    class TINA_CORE_API TextureManager
    {
    public:
        static TextureManager& getInstance();

        SharedPtr<Texture2D> loadTexture(const std::string& name, const std::string& path);
        SharedPtr<Texture2D> getTexture(const std::string& name);
        void releaseTexture(const std::string& name);
        void clear();

    private:
        TextureManager() =default;
        ~TextureManager();
        TextureManager(const TextureManager&) =delete;
        TextureManager& operator=(const TextureManager&) =delete;

        std::unordered_map<std::string, SharedPtr<Texture2D>> m_textures;
    };
} // Tina
