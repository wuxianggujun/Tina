//
// Created by wuxianggujun on 2024/7/17.
//

#include "Resource.hpp"

namespace Tina {
    Resource::Resource(const ResourceHandle &handle, const std::string &path, const ResourceType type):m_handle(handle), m_path(path), m_type(type) {
        
    }

    const ResourceHandle & Resource::getHandle() const {
        return m_handle;
    }

    const std::string & Resource::getPath() const {
        return m_path;
    }

    ResourceType Resource::getType() const {
        return m_type;
    }
} // Tina