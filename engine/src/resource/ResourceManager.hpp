#ifndef TINA_CORE_RESOURCE_MANAGER_HPP
#define TINA_CORE_RESOURCE_MANAGER_HPP

#include <functional>
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

        template<typename T>
        void registerResourceType() {
            m_resourceFactories[T::staticResourceType] = [](const ResourceHandle& handle, const std::string& path) -> RefPtr<Resource> {
                return createRefPtr<T>(handle,path);
            };
        };


    private:
        std::unordered_map<ResourceHandle,RefPtr<Resource>> m_resources;
        std::unordered_map<ResourceType,std::function<RefPtr<Resource>(const ResourceHandle& handle,const std::string& path)>> m_resourceFactories;
    };

 
}


#endif