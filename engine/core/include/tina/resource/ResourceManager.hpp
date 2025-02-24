#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Singleton.hpp"
#include "tina/resource/Resource.hpp"
#include "tina/resource/ResourceLoader.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <future>
#include <vector>

namespace Tina {

/**
 * @brief 资源管理器类
 * 
 * 负责管理所有资源的生命周期，包括：
 * - 资源的加载和卸载
 * - 资源缓存管理
 * - 资源依赖管理
 * - 异步加载队列管理
 */
class TINA_CORE_API ResourceManager : public Singleton<ResourceManager> {
public:
    /**
     * @brief 注册资源加载器
     * @param loader 资源加载器
     */
    void registerLoader(std::unique_ptr<IResourceLoader> loader);

    /**
     * @brief 同步加载资源
     * @param name 资源名称
     * @param path 资源路径
     * @param typeID 资源类型ID
     * @param progressCallback 进度回调
     * @return 资源指针
     */
    template<typename T>
    RefPtr<T> loadSync(const std::string& name, const std::string& path,
        const ResourceLoadProgressCallback& progressCallback = nullptr) {
        static_assert(std::is_base_of_v<Resource, T>, "T must inherit from Resource");
        RefPtr<Resource> resource = loadSync(name, path, T::getStaticTypeID(), progressCallback);
        if (!resource) {
            return RefPtr<T>::null();
        }
        return static_pointer_cast<T>(resource);
    }

    /**
     * @brief 异步加载资源
     * @param name 资源名称
     * @param path 资源路径
     * @param typeID 资源类型ID
     * @param progressCallback 进度回调
     * @return 加载操作的future
     */
    template<typename T>
    std::future<RefPtr<T>> loadAsync(const std::string& name, const std::string& path,
        const ResourceLoadProgressCallback& progressCallback = nullptr) {
        static_assert(std::is_base_of_v<Resource, T>, "T must inherit from Resource");
        auto future = loadAsync(name, path, T::getStaticTypeID(), progressCallback);
        return std::async(std::launch::deferred, [future]() {
            auto resource = future.get();
            if (!resource) {
                return RefPtr<T>::null();
            }
            return static_pointer_cast<T>(resource);
        });
    }

    /**
     * @brief 获取资源
     * @param name 资源名称
     * @return 资源指针
     */
    template<typename T>
    RefPtr<T> getResource(const std::string& name) const {
        static_assert(std::is_base_of_v<Resource, T>, "T must inherit from Resource");
        auto resource = getResource(name);
        if (!resource) {
            return RefPtr<T>::null();
        }
        return static_pointer_cast<T>(resource);
    }

    /**
     * @brief 卸载资源
     * @param name 资源名称
     */
    void unloadResource(const std::string& name);

    /**
     * @brief 卸载所有资源
     */
    void unloadAll();

    /**
     * @brief 更新资源管理器
     * 处理异步加载队列等
     */
    void update();

    /**
     * @brief 关闭资源管理器
     * 
     * 在程序退出前调用此方法以确保所有资源被正确释放。
     * 此方法应该在bgfx关闭前调用。
     */
    void shutdown() {
        TINA_ENGINE_INFO("Shutting down ResourceManager");
        releaseResources();
    }

protected:
    friend class Singleton<ResourceManager>;
    ResourceManager() = default;
    ~ResourceManager();

    /**
     * @brief 在bgfx关闭前释放所有资源
     */
    void releaseResources() {
        std::lock_guard<std::mutex> lock(m_mutex);
        TINA_ENGINE_INFO("Releasing all resources before bgfx shutdown");

        // 先获取所有资源的列表，避免在遍历时修改map
        std::vector<std::string> resourceNames;
        resourceNames.reserve(m_resources.size());
        for (const auto& [name, _] : m_resources) {
            resourceNames.push_back(name);
        }

        // 逐个卸载资源
        for (const auto& name : resourceNames) {
            TINA_ENGINE_DEBUG("Unloading resource: {}", name);
            unloadResource(name);
        }

        // 清空资源映射
        m_resources.clear();

        // 清空加载器
        m_loaders.clear();

        TINA_ENGINE_INFO("All resources released");
    }

private:
    /**
     * @brief 同步加载资源的内部实现
     */
    RefPtr<Resource> loadSync(const std::string& name, const std::string& path,
        ResourceTypeID typeID, const ResourceLoadProgressCallback& progressCallback);

    /**
     * @brief 异步加载资源的内部实现
     */
    std::future<RefPtr<Resource>> loadAsync(const std::string& name, const std::string& path,
        ResourceTypeID typeID, const ResourceLoadProgressCallback& progressCallback);

    /**
     * @brief 获取资源的内部实现
     */
    RefPtr<Resource> getResource(const std::string& name) const;

    /**
     * @brief 获取资源加载器
     */
    IResourceLoader* getLoader(ResourceTypeID typeID) const;

    // 资源加载器映射表
    std::unordered_map<ResourceTypeID, std::unique_ptr<IResourceLoader>> m_loaders;

    // 资源缓存
    std::unordered_map<std::string, RefPtr<Resource>> m_resources;

    // 异步加载队列
    struct AsyncLoadRequest {
        std::string name;
        std::string path;
        ResourceTypeID typeID;
        ResourceLoadProgressCallback progressCallback;
        std::promise<RefPtr<Resource>> promise;
    };
    std::queue<AsyncLoadRequest> m_asyncLoadQueue;

    // 线程同步
    mutable std::mutex m_mutex;
};

} // namespace Tina 