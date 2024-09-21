//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_HPP
#define TINA_CORE_HPP

#include <memory>

#define TINA_STATIC_ASSERT(_condition, ...) static_assert(_condition, "" __VA_ARGS__)

namespace Tina
{
    // Scope is an alias for std::unique_ptr with a default deleter
    template <class T, typename Deleter = std::default_delete<T>>
    using ScopePtr = std::unique_ptr<T,Deleter>;

    // createScope is a helper function to create a Scope object
    template <class T, class... Args>
    constexpr ScopePtr<T> createScopePtr(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    // Ref is an alias for std::shared_ptr
    template <class T>
    using RefPtr = std::shared_ptr<T>;

    // createRef is a helper function to create a Ref object
    template <class T, class... Args>
    constexpr RefPtr<T> createRefPtr(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    // WeakRef is an alias for std::weak_ptr
    template <class T>
    using WeakRefPtr = std::weak_ptr<T>;
} // Tina

#endif //TINA_CORE_HPP
