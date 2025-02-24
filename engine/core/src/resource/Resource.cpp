#include "tina/resource/Resource.hpp"
#include "tina/log/Log.hpp"

namespace Tina {

Resource::Resource(const std::string& name, const std::string& path)
    : m_name(name)
    , m_path(path)
    , m_state(ResourceState::Unloaded)
    , m_size(0)
{
    TINA_ENGINE_DEBUG("Resource constructor: {}, path: {}", name, path);
}

Resource::~Resource() {
    TINA_ENGINE_DEBUG("Resource destructor: {}, refCount: {}", m_name, getRefCount());
}

const std::string& Resource::getName() const {
    return m_name;
}

const std::string& Resource::getPath() const {
    return m_path;
}

ResourceState Resource::getState() const {
    return m_state;
}

size_t Resource::getSize() const {
    return m_size;
}

bool Resource::isLoaded() const {
    return m_state == ResourceState::Loaded;
}

int Resource::getRefCount() const {
    return m_refCount.use_count();
}

void Resource::addRef() {
    TINA_ENGINE_DEBUG("Resource::addRef: {} (before: {})", m_name, getRefCount());
    m_refCount.increment();
    TINA_ENGINE_DEBUG("Resource::addRef: {} (after: {})", m_name, getRefCount());
}

void Resource::release() {
    TINA_ENGINE_DEBUG("Resource::release: {} (before: {})", m_name, getRefCount());
    m_refCount.decrement();
    TINA_ENGINE_DEBUG("Resource::release: {} (after: {})", m_name, getRefCount());
}

} // namespace Tina 