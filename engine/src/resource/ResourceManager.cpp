#include "ResourceManager.hpp"
#include "TextureResource.hpp"
#include "ShaderResource.hpp"

namespace Tina {
    ResourceManager::ResourceManager() {
        // 注册资源类型及其创建函数
        registerResourceType<TextureResource>();
        registerResourceType<ShaderResource>();
    }

    ResourceManager::~ResourceManager() {
        unloadAllResources();
    }

    template<typename T, typename... Args>
    RefPtr<T> ResourceManager::loadResource(const std::string &path, Args &&... args) {
        ResourceHandle handle(path);
        auto it = m_resources.find(handle);
        
        if (it != m_resources.end()) {
            return getResource<T>(handle);
        }

        auto factoryIt = m_resourceFactories.find(T::staticResourceType);
        if (factoryIt == m_resourceFactories.end()) {
            // 处理未知资源类型
            return nullptr;
        }

        const RefPtr<Resource> resource = factoryIt->second(handle, path);
        auto typeResource = std::dynamic_pointer_cast<T>(resource);

        if (!typeResource|| !typeResource->load()) {
            // 加载失败
            return nullptr;
        }
        m_resources[handle] = resource;
        return typeResource;
    }

    template<typename T>
    RefPtr<T> ResourceManager::getResource(const ResourceHandle &handle) {
        const auto it = m_resources.find(handle);
        if (it != m_resources.end()) {
            return std::dynamic_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    void ResourceManager::unloadResource(const ResourceHandle &handle) {
        const auto it = m_resources.find(handle);
        if (it != m_resources.end()) {
            it->second->unload();
            m_resources.erase(it);
        }
    }

    void ResourceManager::unloadAllResources() {
        for (const auto &pair: m_resources) {
            pair.second->unload();
        }
        m_resources.clear();
    }

    template RefPtr<TextureResource> ResourceManager::loadResource<TextureResource>(const std::string& path);
    template RefPtr<ShaderResource> ResourceManager::loadResource<ShaderResource>(const std::string& path);

    template RefPtr<TextureResource> ResourceManager::getResource<TextureResource>(const ResourceHandle& handle);
    template RefPtr<ShaderResource> ResourceManager::getResource<ShaderResource>(const ResourceHandle& handle);
}
