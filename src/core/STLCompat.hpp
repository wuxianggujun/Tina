#pragma once

//
// STL 兼容层 - 将 STL 调用重定向到 EASTL
// 用于统一使用 EASTL，避免 STL/EASTL 混用带来的性能问题
//

#include "Container.hpp"
#include <cstring>  // 保留 C 标准库（memcpy, memset 等）
#include <cmath>    // 数学函数
#include <cstdio>   // 文件 I/O
#include <cstdlib>  // 标准工具

namespace Tina {

// ==================== STL 到 EASTL 的映射 ====================

namespace stl {
    // 算法映射
    using Tina::Container::Sort;
    using Tina::Container::Find;
    using Tina::Container::FindIf;
    using Tina::Container::Copy;
    using Tina::Container::Fill;
    using Tina::Container::Count;
    using Tina::Container::CountIf;
    using Tina::Container::Remove;
    using Tina::Container::RemoveIf;
    using Tina::Container::Transform;
    using Tina::Container::Reverse;
    using Tina::Container::Min;
    using Tina::Container::Max;

    // 额外的算法包装
    template<typename Iterator, typename Compare>
    inline void sort(Iterator first, Iterator last, Compare comp) {
        eastl::sort(first, last, comp);
    }

    template<typename Iterator>
    inline void sort(Iterator first, Iterator last) {
        eastl::sort(first, last);
    }

    template<typename Container>
    inline void sort(Container& container) {
        eastl::sort(container.begin(), container.end());
    }

    // stable_sort
    template<typename Iterator>
    inline void stable_sort(Iterator first, Iterator last) {
        eastl::stable_sort(first, last);
    }

    template<typename Iterator, typename Compare>
    inline void stable_sort(Iterator first, Iterator last, Compare comp) {
        eastl::stable_sort(first, last, comp);
    }

    // unique
    template<typename Iterator>
    inline Iterator unique(Iterator first, Iterator last) {
        return eastl::unique(first, last);
    }

    // swap
    template<typename T>
    inline void swap(T& a, T& b) {
        eastl::swap(a, b);
    }

    // move
    template<typename T>
    inline typename eastl::remove_reference<T>::type&& move(T&& t) {
        return eastl::move(t);
    }

    // forward
    template<typename T>
    inline T&& forward(typename eastl::remove_reference<T>::type& t) {
        return eastl::forward<T>(t);
    }

    template<typename T>
    inline T&& forward(typename eastl::remove_reference<T>::type&& t) {
        return eastl::forward<T>(t);
    }

    // min_element / max_element
    template<typename Iterator>
    inline Iterator min_element(Iterator first, Iterator last) {
        return eastl::min_element(first, last);
    }

    template<typename Iterator, typename Compare>
    inline Iterator min_element(Iterator first, Iterator last, Compare comp) {
        return eastl::min_element(first, last, comp);
    }

    template<typename Iterator>
    inline Iterator max_element(Iterator first, Iterator last) {
        return eastl::max_element(first, last);
    }

    template<typename Iterator, typename Compare>
    inline Iterator max_element(Iterator first, Iterator last, Compare comp) {
        return eastl::max_element(first, last, comp);
    }

    // lower_bound / upper_bound
    template<typename Iterator, typename T>
    inline Iterator lower_bound(Iterator first, Iterator last, const T& value) {
        return eastl::lower_bound(first, last, value);
    }

    template<typename Iterator, typename T, typename Compare>
    inline Iterator lower_bound(Iterator first, Iterator last, const T& value, Compare comp) {
        return eastl::lower_bound(first, last, value, comp);
    }

    template<typename Iterator, typename T>
    inline Iterator upper_bound(Iterator first, Iterator last, const T& value) {
        return eastl::upper_bound(first, last, value);
    }

    template<typename Iterator, typename T, typename Compare>
    inline Iterator upper_bound(Iterator first, Iterator last, const T& value, Compare comp) {
        return eastl::upper_bound(first, last, value, comp);
    }

    // binary_search
    template<typename Iterator, typename T>
    inline bool binary_search(Iterator first, Iterator last, const T& value) {
        return eastl::binary_search(first, last, value);
    }

    template<typename Iterator, typename T, typename Compare>
    inline bool binary_search(Iterator first, Iterator last, const T& value, Compare comp) {
        return eastl::binary_search(first, last, value, comp);
    }

    // for_each
    template<typename Iterator, typename Function>
    inline Function for_each(Iterator first, Iterator last, Function f) {
        return eastl::for_each(first, last, f);
    }

    // all_of / any_of / none_of
    template<typename Iterator, typename Predicate>
    inline bool all_of(Iterator first, Iterator last, Predicate pred) {
        return eastl::all_of(first, last, pred);
    }

    template<typename Iterator, typename Predicate>
    inline bool any_of(Iterator first, Iterator last, Predicate pred) {
        return eastl::any_of(first, last, pred);
    }

    template<typename Iterator, typename Predicate>
    inline bool none_of(Iterator first, Iterator last, Predicate pred) {
        return eastl::none_of(first, last, pred);
    }

    // 数学函数保持使用标准库
    using ::cos;
    using ::sin;
    using ::tan;
    using ::acos;
    using ::asin;
    using ::atan;
    using ::atan2;
    using ::sqrt;
    using ::pow;
    using ::exp;
    using ::log;
    using ::log10;
    using ::abs;
    using ::fabs;
    using ::floor;
    using ::ceil;
    using ::round;
    using ::fmod;

    // C 标准库函数保持不变
    using ::memcpy;
    using ::memmove;
    using ::memset;
    using ::memcmp;
    using ::strlen;
    using ::strcmp;
    using ::strncmp;
    using ::strcpy;
    using ::strncpy;
    using ::strcat;
    using ::strncat;

} // namespace stl

// ==================== 全局命名空间污染防护 ====================

// 如果项目中有使用 std:: 的地方，可以通过宏重定向
// 注意：这是可选的，可能会导致编译问题，谨慎使用
#ifdef TINA_FORCE_EASTL
    #define std Tina::stl
#endif

} // namespace Tina