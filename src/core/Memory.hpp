//
// 内存管理 - 智能指针和内存工具
//

#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/weak_ptr.h>

namespace Tina {
namespace Memory {

    // ====================
    // 智能指针
    // ====================

    // 独占指针
    template<typename T, typename Deleter = eastl::default_delete<T>>
    using UniquePtr = eastl::unique_ptr<T, Deleter>;

    // 共享指针
    template<typename T>
    using SharedPtr = eastl::shared_ptr<T>;

    // 弱指针
    template<typename T>
    using WeakPtr = eastl::weak_ptr<T>;

    // ====================
    // 便捷创建函数
    // ====================

    // 创建独占指针
    template<typename T, typename... Args>
    inline UniquePtr<T> MakeUnique(Args&&... args) {
        return eastl::make_unique<T>(eastl::forward<Args>(args)...);
    }

    // 创建共享指针
    template<typename T, typename... Args>
    inline SharedPtr<T> MakeShared(Args&&... args) {
        return eastl::make_shared<T>(eastl::forward<Args>(args)...);
    }

} // namespace Memory
} // namespace Tina
