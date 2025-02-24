#include "tina/resource/ResourceManager.hpp"
#include "tina/resource/ShaderResource.hpp"
#include "tina/log/Log.hpp"
#include <filesystem>

namespace Tina {

ResourceManager::~ResourceManager() noexcept {
    try {
        TINA_ENGINE_INFO("ResourceManager destructor called");
        
        // 确保所有资源都被正确卸载
        unloadAll();
        
        TINA_ENGINE_DEBUG("Clearing resource loaders");
        m_loaders.clear();
        
        TINA_ENGINE_INFO("ResourceManager cleanup completed");
    } catch (const std::exception& e) {
        TINA_ENGINE_ERROR("Exception during ResourceManager destruction: {}", e.what());
    } catch (...) {
        TINA_ENGINE_ERROR("Unknown exception during ResourceManager destruction");
    }
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
    // 线程安全
    std::lock_guard<std::mutex> lock(m_mutex);

    TINA_ENGINE_DEBUG("Attempting to load resource '{}' from path: {}", name, path);

    // 检查资源是否已加载
    auto it = m_resources.find(name);
    if (it != m_resources.end()) {
        TINA_ENGINE_DEBUG("Found existing resource '{}', refCount: {}, state: {}", 
            name, it->second->getRefCount(), (int)it->second->getState());
            
        if (it->second->isLoaded()) {
            TINA_ENGINE_DEBUG("Resource '{}' already loaded", name);
            return it->second;
        }
        if (it->second->getState() == ResourceState::Loading) {
            TINA_ENGINE_WARN("Resource '{}' is currently loading", name);
            return RefPtr<Resource>::null();
        }
    }

    // 2. 获取对应的资源加载器
    IResourceLoader* loader = getLoader(typeID);
    if (!loader) {
        TINA_ENGINE_ERROR("No loader found for resource type {}", typeID);
        return RefPtr<Resource>::null();
    }

    // 3. 创建资源实例
    RefPtr<Resource> resource;
    if (it != m_resources.end()) {
        resource = it->second;  // 使用已存在的实例
    } else {
        // 让加载器创建资源实例
        resource = loader->createResource(name, path);
        if (!resource) {
            TINA_ENGINE_ERROR("Failed to create resource instance for '{}'", name);
            return RefPtr<Resource>::null();
        }
        
        TINA_ENGINE_DEBUG("Created new resource instance '{}', initial refCount: {}", 
            name, resource->getRefCount());
        
        // 缓存新创建的资源
        m_resources[name] = resource;
    }

    // 4. 加载资源内容
    TINA_ENGINE_INFO("Loading resource '{}' from {}", name, path);
    bool success = loader->loadSync(resource.get(), progressCallback);
    // 5. 处理加载结果
    if (!success) {
        TINA_ENGINE_ERROR("Failed to load resource '{}'", name);
        m_resources.erase(name);  // 移除失败的资源
        return RefPtr<Resource>::null();
    }

    TINA_ENGINE_INFO("Successfully loaded resource '{}', final refCount: {}", 
        name, resource->getRefCount());
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

    TINA_ENGINE_DEBUG("Created async load request for resource '{}'", name);

    // 将请求添加到队列
    m_asyncLoadQueue.push(std::move(request));

    // 返回future
    return request.promise.get_future();
}

RefPtr<Resource> ResourceManager::getResource(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(name);
    if (it != m_resources.end()) {
        TINA_ENGINE_DEBUG("Found resource '{}', refCount: {}", name, it->second->getRefCount());
        return it->second;
    }
    TINA_ENGINE_DEBUG("Resource '{}' not found", name);
    return RefPtr<Resource>::null();
}

void ResourceManager::unloadResource(const std::string& name) {
    // 注意:调用此方法时需要确保已经获得了m_mutex锁
    auto it = m_resources.find(name);
    if (it != m_resources.end()) {
        auto& resource = it->second;
        if (!resource) {
            TINA_ENGINE_WARN("Null resource found with name: {}", name);
            m_resources.erase(it);
            return;
        }

        TINA_ENGINE_DEBUG("Unloading resource '{}', current refCount: {}", 
            name, resource->getRefCount());
            
        if (IResourceLoader* loader = getLoader(resource->getTypeID())) {
            try {
                TINA_ENGINE_INFO("Unloading resource '{}'", name);
                loader->unload(resource.get());
            }
            catch (const std::exception& e) {
                TINA_ENGINE_ERROR("Exception while unloading resource '{}': {}", name, e.what());
            }
            catch (...) {
                TINA_ENGINE_ERROR("Unknown exception while unloading resource '{}'", name);
            }
        }

        // 从map中移除资源前记录引用计数
        int refCount = resource->getRefCount();
        TINA_ENGINE_DEBUG("Resource '{}' refCount before removal: {}", name, refCount);

        // 从map中移除资源，这会减少一个引用
        m_resources.erase(it);

        TINA_ENGINE_DEBUG("Resource '{}' removed from resource map", name);
    }
}

void ResourceManager::unloadAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    TINA_ENGINE_INFO("Unloading all resources, count: {}", m_resources.size());

    if (m_resources.empty()) {
        return;
    }

    // 在修改任何资源之前,先获取所有资源的弱引用
    std::vector<std::pair<std::string, Resource*>> resources;
    resources.reserve(m_resources.size());

    for (const auto& [name, resource] : m_resources) {
        if (!resource) {
            TINA_ENGINE_WARN("Null resource found with name: {}", name);
            continue;
        }

        // 检查引用计数
        auto refCount = resource->getRefCount();
        if (refCount <= 0) {
            TINA_ENGINE_ERROR("Resource '{}' has invalid reference count: {}", name, refCount);
            continue;
        }

        TINA_ENGINE_DEBUG("Preparing to unload resource: {}, refCount: {}", 
            name, refCount);
        
        // 只存储原始指针,不增加引用计数
        resources.emplace_back(name, resource.get());
    }

    // 先卸载所有资源的内容
    for (auto& [name, resource] : resources) {
        if (!resource) {
            continue;
        }

        TINA_ENGINE_DEBUG("Unloading resource content: {}, refCount: {}", 
            name, resource->getRefCount());

        auto* loader = getLoader(resource->getTypeID());
        if (!loader) {
            TINA_ENGINE_WARN("No loader found for resource: {}", name);
            continue;
        }

        try {
            if (resource->isLoaded()) {
                TINA_ENGINE_DEBUG("Resource {} is loaded, calling unload", name);
                loader->unload(resource);
            } else {
                TINA_ENGINE_DEBUG("Resource {} is not loaded, skipping unload", name);
            }
        } catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Failed to unload resource '{}': {}", name, e.what());
        } catch (...) {
            TINA_ENGINE_ERROR("Unknown error while unloading resource '{}'", name);
        }
    }

    // 清空资源映射,这会减少每个资源的引用计数
    m_resources.clear();
    TINA_ENGINE_DEBUG("Resource map cleared");

    // 记录最终的引用计数
    for (const auto& [name, resource] : resources) {
        if (!resource) continue;
        
        auto refCount = resource->getRefCount();
        TINA_ENGINE_DEBUG("Resource '{}' has {} reference(s) after unload", 
            name, refCount);
    }

    // 清空临时vector,这不会影响引用计数因为只存储了原始指针
    resources.clear();
    TINA_ENGINE_INFO("Resource unloading completed");
}

void ResourceManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 处理异步加载队列
    while (!m_asyncLoadQueue.empty()) {
        AsyncLoadRequest& request = m_asyncLoadQueue.front();
        
        TINA_ENGINE_DEBUG("Processing async load request for resource '{}'", request.name);
        
        // 执行异步加载
        auto resource = loadSync(request.name, request.path, 
            request.typeID, request.progressCallback);

        // 设置结果
        request.promise.set_value(resource);

        // 移除请求
        m_asyncLoadQueue.pop();
        
        TINA_ENGINE_DEBUG("Completed async load request for resource '{}'", request.name);
    }
}

IResourceLoader* ResourceManager::getLoader(ResourceTypeID typeID) const {
    auto it = m_loaders.find(typeID);
    if (it != m_loaders.end()) {
        return it->second.get();
    }
    TINA_ENGINE_DEBUG("No loader found for resource type {}", typeID);
    return nullptr;
}

} // namespace Tina 