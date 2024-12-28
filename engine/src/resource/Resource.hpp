//
// Created by wuxianggujun on 2024/7/17.
//

#ifndef TINA_CORE_RESOURCE_HPP
#define TINA_CORE_RESOURCE_HPP

#include <string>
#include "ResourceHandle.hpp"
#include "ResourceType.hpp"
#include "core/Core.hpp"

namespace Tina {
    class Resource {
    public:
        Resource(const ResourceHandle &handle, const std::string &path, ResourceType type);

        virtual ~Resource() = default;
        

        [[nodiscard]] const ResourceHandle &getHandle() const;
        [[nodiscard]] const std::string &getPath() const;
        [[nodiscard]] ResourceType getType() const;
        [[nodiscard]] virtual bool isLoaded() const = 0;
        
        
    protected:
        ResourceHandle m_handle;
        std::string m_path;
        ResourceType m_type;
    };
} // Tina

#endif
