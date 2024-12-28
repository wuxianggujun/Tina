#ifndef TINA_CORE_RESOURCE_MANAGER_HPP
#define TINA_CORE_RESOURCE_MANAGER_HPP

#include <unordered_map>
#include <string>
#include "ResourceHandle.hpp"
#include "Resource.hpp"
#include "core/Core.hpp"

namespace Tina {
    class ResourceManager {

    public:
        ResourceManager();
        ~ResourceManager();

        template<typename T,typename... Args>
        RefPtr<T> loadResource(const std::string& path, Args&&... args);

        template<typename T>
        RefPtr<T> getResource(const ResourceHandle& handle);

        void unloadResource(const ResourceHandle& handle);
        void unloadAllResources();
        void registerResourceType(ResourceType type,
                                  RefPtr<Resource> (*createFunc)(const ResourceHandle&, const std::string&));


    private:
        std::unordered_map<ResourceHandle,RefPtr<Resource>> m_resources;
        std::unordered_map<ResourceType,RefPtr<Resource>(*)(const ResourceHandle& handle, const std::string& path)> m_resourceFactories;
    };

 
}


#endif