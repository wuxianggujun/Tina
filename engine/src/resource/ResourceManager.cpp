#include "ResourceManager.hpp"
#include "TextureResource.hpp"
#include "ShaderResource.hpp"

namespace Tina {
    ResourceManager::ResourceManager() {
        // 注册资源类型及其创建函数
        registerResourceType(ResourceType::Texture, [](const ResourceHandle &handle, const std::string &path) {
            return createRefPtr<TextureResource>(handle, path);
        });

        registerResourceType(ResourceType::Shader, [](const ResourceHandle &handle, const std::string &path) {
            return createRefPtr<ShaderResource>(handle, path);
        });
    }

    ResourceManager::~ResourceManager() {
        unloadAllResources();
    }

    template<typename T, typename ... Args>
 RefPtr<T> ResourceManager::loadResource(const std::string &path, Args &&...args) {
        ResourceHandle handle(path);
        if (m_resources.contains(handle)) {
            return getResource<T>(handle);
        }

        auto it= m_resourceFactories.find(T::staticResourceType);
        if (it == m_resourceFactories.end()) {
            // 处理未知资源类型
            return nullptr;
        }

        RefPtr<Resource> resource = it->second(handle,path);
        RefPtr<T> typeResource = std::static_pointer_cast<T>(resource);

        if (!typeResource->load()) {
            // 加载失败
            return nullptr;
        }
        m_resources[handle] = resource;
        return typeResource;
    }

    template <typename T>
   RefPtr<T> ResourceManager::getResource(const ResourceHandle& handle) {
        auto it = m_resources.find(handle);
        if (it != m_resources.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    void ResourceManager::unloadResource(const ResourceHandle& handle) {
        auto it = m_resources.find(handle);
        if (it != m_resources.end()) {
            it->second->unload();
            m_resources.erase(it);
        }
    }

    void ResourceManager::unloadAllResources() {
        for (auto& pair : m_resources) {
            pair.second->unload();
        }
        m_resources.clear();
    }

    void ResourceManager::registerResourceType(ResourceType type,
                                               RefPtr<Resource> (*createFunc)(const ResourceHandle&, const std::string&)) {
        m_resourceFactories[type] = createFunc;
    }
}
