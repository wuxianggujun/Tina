#ifndef TINA_CORE_RESOURCE_HANDLE_HPP
#define TINA_CORE_RESOURCE_HANDLE_HPP

#include <string>
#include <cstdint>

namespace Tina {
    class ResourceHandle {
    public:
        ResourceHandle();

        explicit ResourceHandle(uint64_t id);

        explicit ResourceHandle(const std::string &path);
        
        [[nodiscard]] uint64_t getId() const;
        [[nodiscard]] bool isValid() const;

        bool operator==(const ResourceHandle &other) const;
        
    private:
        uint64_t m_id;
    };
}


// 特化 std::hash
namespace std {
    template <>
    struct hash<Tina::ResourceHandle> {
        size_t operator()(const Tina::ResourceHandle& handle) const noexcept {
            return handle.getId();
        }
    };
}
#endif
