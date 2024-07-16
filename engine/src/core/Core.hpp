//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_HPP
#define TINA_CORE_HPP

#include <memory>

namespace Tina
{

    template <class T>
    using Scope = std::unique_ptr<T>;

    template <class T, class... Args>
    constexpr Scope<T> createScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template <class T>
    using Ref = std::shared_ptr<T>;

    template <class T, class... Args>
    constexpr Ref<T> createRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
} // Tina

#endif //TINA_CORE_HPP
