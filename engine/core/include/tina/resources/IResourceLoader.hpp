#pragma once

#include <memory>
#include <string>
#include "tina/core/Core.hpp"
#include "tina/resources/Resource.hpp"

namespace Tina {

class TINA_CORE_API IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    
    // 加载资源
    virtual std::shared_ptr<Resource> load(const std::string& path) = 0;
    
    // 重新加载资源
    virtual bool reload(Resource* resource) = 0;
};

} // namespace Tina
