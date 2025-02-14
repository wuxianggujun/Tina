#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <typeindex>
#include <filesystem>
#include <functional>
#include <mutex>
#include "tina/utils/Profiler.hpp"

// 自定义哈希函数
namespace std {
    template<>
    struct hash<pair<type_index, string>> {
        using argument_type = pair<type_index, string>;
        using result_type = size_t;

        result_type operator()(const argument_type& p) const noexcept {
            return hash<type_index>()(p.first) ^ (hash<string>()(p.second) << 1);
        }
    };
}

namespace Tina {

// 资源基类
class Resource {
public:
    virtual ~Resource() = default;
    virtual bool isValid() const = 0;
    virtual bool reload() = 0;
};

// 资源加载器接口
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    virtual std::shared_ptr<Resource> load(const std::string& path) = 0;
    virtual bool reload(Resource* resource) = 0;
};

// 资源管理器
class ResourceManager {
public:
    static ResourceManager& getInstance() {
        static ResourceManager instance;
        return instance;
    }

    // 注册资源加载器
    template<typename T>
    void registerLoader(std::unique_ptr<IResourceLoader> loader) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Loaders[typeid(T)] = std::move(loader);
    }

    // 加载资源
    template<typename T>
    std::shared_ptr<T> load(const std::string& path) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 检查资源是否已加载
        auto fullPath = std::filesystem::absolute(path).string();
        auto key = getResourceKey<T>(fullPath);
        auto it = m_Resources.find(key);
        if (it != m_Resources.end()) {
            if (auto resource = std::dynamic_pointer_cast<T>(it->second.lock())) {
                return resource;
            }
            m_Resources.erase(it);
        }

        // 获取加载器
        auto loaderIt = m_Loaders.find(typeid(T));
        if (loaderIt == m_Loaders.end()) {
            throw std::runtime_error("No loader registered for resource type");
        }

        // 加载资源
        auto resource = std::dynamic_pointer_cast<T>(loaderIt->second->load(fullPath));
        if (resource) {
            m_Resources[key] = resource;
            
            // 添加文件监视
            if (m_HotReloadEnabled) {
                addFileWatch(fullPath, [this, key]() {
                    reloadResource(key);
                });
            }
        }

        return resource;
    }

    // 重新加载资源
    bool reload(const std::string& path) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto fullPath = std::filesystem::absolute(path).string();
        bool success = true;
        
        // 重新加载所有使用此路径的资源
        for (auto it = m_Resources.begin(); it != m_Resources.end(); ) {
            if (it->first.second == fullPath) {
                if (!reloadResource(it->first)) {
                    success = false;
                }
                ++it;
            } else {
                ++it;
            }
        }
        
        return success;
    }

    // 启用/禁用热重载
    void setHotReloadEnabled(bool enabled) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (m_HotReloadEnabled == enabled) return;
        
        m_HotReloadEnabled = enabled;
        if (enabled) {
            startFileWatcher();
        } else {
            stopFileWatcher();
        }
    }

    // 清理未使用的资源
    void cleanup() {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        for (auto it = m_Resources.begin(); it != m_Resources.end(); ) {
            if (it->second.expired()) {
                it = m_Resources.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    ResourceManager() = default;
    ~ResourceManager() {
        stopFileWatcher();
    }

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 获取资源键
    template<typename T>
    std::pair<std::type_index, std::string> getResourceKey(const std::string& path) {
        return std::make_pair(std::type_index(typeid(T)), path);
    }

    // 重新加载指定资源
    bool reloadResource(const std::pair<std::type_index, std::string>& key) {
        auto it = m_Resources.find(key);
        if (it == m_Resources.end()) return false;

        auto resource = it->second.lock();
        if (!resource) {
            m_Resources.erase(it);
            return false;
        }

        auto loaderIt = m_Loaders.find(key.first);
        if (loaderIt == m_Loaders.end()) return false;

        return loaderIt->second->reload(resource.get());
    }

    // 文件监视相关
    void startFileWatcher();
    void stopFileWatcher();
    void addFileWatch(const std::string& path, std::function<void()> callback);

private:
    std::unordered_map<std::type_index, std::unique_ptr<IResourceLoader>> m_Loaders;
    std::unordered_map<std::pair<std::type_index, std::string>, std::weak_ptr<Resource>> m_Resources;
    bool m_HotReloadEnabled = false;
    std::mutex m_Mutex;

    // TODO: 添加文件监视系统
};

} // namespace Tina 