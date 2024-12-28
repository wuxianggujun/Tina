#ifndef TINA_CORE_RESOURCE_HANDLE_HPP
#define TINA_CORE_RESOURCE_HANDLE_HPP

#include <string>
#include <cstdint>

namespace Tina {
    class ResourceHandle {
    public:
        ResourceHandle(): m_id(0) {
        }

        explicit ResourceHandle(uint64_t id): m_id(id) {
        }

        explicit ResourceHandle(const std::string &path): m_id(std::hash<std::string>{}(path)) {
        }

        [[nodiscard]] uint64_t getId() const;
        [[nodiscard]] bool isValid() const;

        bool operator==(const ResourceHandle & other) const;
        bool operator!=(const ResourceHandle & other) const;
        // 用于std::map排序
        bool operator<(const ResourceHandle & other) const;
    
    private:
        uint64_t m_id;
    };
}
#endif
