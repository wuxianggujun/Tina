#include "ResourceHandle.hpp"

namespace Tina {
    uint64_t ResourceHandle::getId() const {
        return m_id;
    }

    bool ResourceHandle::isValid() const {
        return m_id != 0;
    }
}
