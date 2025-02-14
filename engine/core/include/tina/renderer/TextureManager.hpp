//
// Created by wuxianggujun on 2025/2/14.
//

#pragma once

#include "Texture2D.hpp"
#include "tina/core/Core.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "tina/log/Logger.hpp"

namespace Tina
{
    class TINA_CORE_API TextureManager
    {
    public:
        static TextureManager& getInstance();

        // 禁止拷贝和赋值
        TextureManager(const TextureManager&) = delete;
        TextureManager& operator=(const TextureManager&) = delete;

        // 加载纹理
        SharedPtr<Texture2D> loadTexture(const std::string& name, const std::string& path);
        
        // 获取纹理
        SharedPtr<Texture2D> getTexture(const std::string& name);
        
        // 释放纹理
        void releaseTexture(const std::string& name);
        
        // 清理所有纹理
        void clear();

        void shutdown();

    private:
        TextureManager() = default;
        ~TextureManager()
        {
            // 析构函数中不再进行资源清理
            TINA_LOG_DEBUG("TextureManager destroyed");
        }

        std::unordered_map<std::string, SharedPtr<Texture2D>> m_textures;
        mutable std::mutex m_mutex;  // 用于线程同步
    };
} // Tina
