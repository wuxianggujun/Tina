#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <typeindex>
#include <filesystem>
#include <functional>
#include <mutex>
#include "tina/core/Core.hpp"
#include "tina/log/Logger.hpp"
#include "tina/utils/Profiler.hpp"
#include "tina/resources/Resource.hpp"
#include "tina/resources/IResourceLoader.hpp"

// 自定义哈希函数
namespace std
{
    template <>
    struct hash<std::pair<std::type_index, std::string>>
    {
        size_t operator()(const std::pair<std::type_index, std::string>& p) const noexcept
        {
            return hash<std::string>()(p.second) ^ (hash<std::type_index>()(p.first) << 1);
        }
    };
}

namespace Tina
{
    class TINA_CORE_API ResourceManager
    {
    public:
        static ResourceManager& getInstance()
        {
            static ResourceManager instance;
            return instance;
        }

        // 注册资源加载器
        template <typename T>
        void registerLoader(std::unique_ptr<IResourceLoader> loader)
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Loaders[typeid(T)] = std::move(loader);
        }

        // 加载资源
        template <typename T>
        std::shared_ptr<T> load(const std::string& path)
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);

            // 检查资源是否已加载
            auto fullPath = std::filesystem::absolute(path).string();
            auto key = getResourceKey<T>(fullPath);
            auto it = m_Resources.find(key);
            if (it != m_Resources.end())
            {
                if (auto resource = std::dynamic_pointer_cast<T>(it->second.lock()))
                {
                    return resource;
                }
                m_Resources.erase(it);
            }

            // 获取加载器
            auto loaderIt = m_Loaders.find(typeid(T));
            if (loaderIt == m_Loaders.end())
            {
                TINA_CORE_LOG_ERROR("No loader registered for resource type");
                return nullptr;
            }

            // 加载资源
            auto resource = std::dynamic_pointer_cast<T>(loaderIt->second->load(fullPath));
            if (resource)
            {
                m_Resources[key] = resource;
            }

            return resource;
        }

        // 获取已加载的资源
        template <typename T>
        std::shared_ptr<T> get(const std::string& path)
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto fullPath = std::filesystem::absolute(path).string();
            auto key = getResourceKey<T>(fullPath);
            auto it = m_Resources.find(key);
            if (it != m_Resources.end())
            {
                return std::dynamic_pointer_cast<T>(it->second.lock());
            }
            return nullptr;
        }

        // 释放指定资源
        template <typename T>
        void release(const std::string& path)
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto fullPath = std::filesystem::absolute(path).string();
            auto key = getResourceKey<T>(fullPath);
            auto it = m_Resources.find(key);
            if (it != m_Resources.end())
            {
                if (auto resource = it->second.lock())
                {
                    resource->release();
                }
                m_Resources.erase(it);
            }
        }

        // 释放所有资源
        void releaseAll()
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);

            for (auto it = m_Resources.begin(); it != m_Resources.end();)
            {
                if (auto resource = it->second.lock())
                {
                    resource->release();
                }
                it = m_Resources.erase(it);
            }

            TINA_CORE_LOG_INFO("All resources released");
        }

        // 重新加载资源
        bool reload(const std::string& path)
        {
            TINA_PROFILE_FUNCTION();
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto fullPath = std::filesystem::absolute(path).string();
            bool success = true;

            for (auto it = m_Resources.begin(); it != m_Resources.end(); ++it)
            {
                if (it->first.second == fullPath)
                {
                    if (auto resource = it->second.lock())
                    {
                        if (!resource->reload())
                        {
                            success = false;
                        }
                    }
                }
            }

            return success;
        }

    private:
        ResourceManager() = default;

        ~ResourceManager()
        {
            releaseAll();
        }

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        // 获取资源键
        template <typename T>
        std::pair<std::type_index, std::string> getResourceKey(const std::string& path)
        {
            return std::make_pair(std::type_index(typeid(T)), path);
        }

        std::unordered_map<std::type_index, std::unique_ptr<IResourceLoader>> m_Loaders;
        std::unordered_map<std::pair<std::type_index, std::string>, std::weak_ptr<Resource>> m_Resources;
        std::mutex m_Mutex;
    };
} // namespace Tina
