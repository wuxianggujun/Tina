#include "tina/resource/ResourceManager.hpp"
#include "tina/log/Log.hpp"
#include <filesystem>

namespace Tina {

ResourceManager::~ResourceManager() {
    unloadAll();
}

void ResourceManager::registerLoader(std::unique_ptr<IResourceLoader> loader) {
    if (!loader) {
        TINA_ENGINE_ERROR("Attempting to register null resource loader");
        return;
    }

    ResourceTypeID typeID = loader->getResourceTypeID();
    auto it = m_loaders.find(typeID);
    if (it != m_loaders.end()) {
        TINA_ENGINE_WARN("Resource loader for type {} already exists, replacing", typeID);
    }

    m_loaders[typeID] = std::move(loader);
    TINA_ENGINE_INFO("Registered resource loader for type {}", typeID);
}

RefPtr<Resource> ResourceManager::loadSync(const std::string& name, const std::string& path,
    ResourceTypeID typeID, const ResourceLoadProgressCallback& progressCallback) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 检查资源是否已加载
    auto it = m_resources.find(name);
    if (it != m_resources.end()) {
        if (it->second->isLoaded()) {
            TINA_ENGINE_DEBUG("Resource '{}' already loaded", name);
            return it->second;
        }
        else if (it->second->getState() == ResourceState::Loading) {
            TINA_ENGINE_WARN("Resource '{}' is currently loading", name);
            return nullptr;
        }
    }

    // 获取加载器
    IResourceLoader* loader = getLoader(typeID);
    if (!loader) {
        TINA_ENGINE_ERROR("No loader found for resource type {}", typeID);
        return nullptr;
    }

    // 检查文件是否存在
    if (!std::filesystem::exists(path)) {
        TINA_ENGINE_ERROR("Resource file not found: {}", path);
        return nullptr;
    }

    // 创建资源实例
    RefPtr<Resource> resource = it != m_resources.end() ? it->second : nullptr;
    if (!resource) {
        // TODO: 使用工厂模式创建具体资源类型的实例
        TINA_ENGINE_ERROR("Resource creation not implemented for type {}", typeID);
        return nullptr;
    }

    // 加载资源
    TINA_ENGINE_INFO("Loading resource '{}' from {}", name, path);
    bool success = loader->loadSync(resource.get(), progressCallback);

    if (!success) {
        TINA_ENGINE_ERROR("Failed to load resource '{}'", name);
        return nullptr;
    }

    // 缓存资源
    m_resources[name] = resource;
    TINA_ENGINE_INFO("Successfully loaded resource '{}'", name);

    return resource;
}

std::future<RefPtr<Resource>> ResourceManager::loadAsync(const std::string& name, 
    const std::string& path, ResourceTypeID typeID, 
    const ResourceLoadProgressCallback& progressCallback) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 创建异步加载请求
    AsyncLoadRequest request;
    request.name = name;
    request.path = path;
    request.typeID = typeID;
    request.progressCallback = progressCallback;

    // 将请求添加到队列
    m_asyncLoadQueue.push(std::move(request));

    // 返回future
    return request.promise.get_future();
}

RefPtr<Resource> ResourceManager::getResource(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(name);
    return it != m_resources.end() ? it->second : nullptr;
}

void ResourceManager::unloadResource(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resources.find(name);
    if (it != m_resources.end()) {
        if (IResourceLoader* loader = getLoader(it->second->getTypeID())) {
            TINA_ENGINE_INFO("Unloading resource '{}'", name);
            loader->unload(it->second.get());
        }
        m_resources.erase(it);
    }
}

void ResourceManager::unloadAll() {
    std::lock_guard<std::mutex> lock(m_mutex);

    TINA_ENGINE_INFO("Unloading all resources");
    for (auto& [name, resource] : m_resources) {
        if (IResourceLoader* loader = getLoader(resource->getTypeID())) {
            loader->unload(resource.get());
        }
    }
    m_resources.clear();
}

void ResourceManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 处理异步加载队列
    while (!m_asyncLoadQueue.empty()) {
        AsyncLoadRequest& request = m_asyncLoadQueue.front();
        
        // 执行异步加载
        auto resource = loadSync(request.name, request.path, 
            request.typeID, request.progressCallback);

        // 设置结果
        request.promise.set_value(resource);

        // 移除请求
        m_asyncLoadQueue.pop();
    }
}

IResourceLoader* ResourceManager::getLoader(ResourceTypeID typeID) const {
    auto it = m_loaders.find(typeID);
    return it != m_loaders.end() ? it->second.get() : nullptr;
}

} // namespace Tina 