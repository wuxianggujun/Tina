#include "ResourceHandle.hpp"

namespace Tina {
    ResourceHandle::ResourceHandle():m_id(0) {
        
    }

    ResourceHandle::ResourceHandle(uint64_t id):m_id(id) {
    }

    ResourceHandle::ResourceHandle(const std::string &path): m_id(std::hash<std::string>{}(path)) {
    }

    uint64_t ResourceHandle::getId() const {
        return m_id;
    }

    bool ResourceHandle::isValid() const {
        return m_id != 0;
    }

    bool ResourceHandle::operator==(const ResourceHandle &other) const {
        return m_id == other.m_id;
    }
}
