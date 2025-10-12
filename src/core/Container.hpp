#pragma once

#include "Core.hpp"

// EASTL headers - 常用容器
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/map.h>
#include <EASTL/set.h>
#include <EASTL/deque.h>
#include <EASTL/list.h>
#include <EASTL/queue.h>
#include <EASTL/stack.h>
#include <EASTL/bitset.h>
#include <EASTL/functional.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <EASTL/memory.h>
#include <EASTL/fixed_hash_map.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/fixed_function.h>
#include <EASTL/bonus/ring_buffer.h>

#include "EASTL/priority_queue.h"

// 注意：智能指针已移至 Memory.hpp（Core.hpp 会自动引入）

namespace Tina {
    namespace Container {
        
        // ====================
        // 序列容器 (Sequence Containers)
        // ====================
        
        // 动态数组
        template<typename T, typename Allocator = eastl::allocator>
        using Vector = eastl::vector<T, Allocator>;
        
        // 固定大小数组
        template<typename T, size_t N>
        using Array = eastl::array<T, N>;
        
        // 双端队列
        template<typename T, typename Allocator = eastl::allocator>
        using Deque = eastl::deque<T, Allocator>;
        
        // 双向链表
        template<typename T, typename Allocator = eastl::allocator>
        using List = eastl::list<T, Allocator>;
        
        // ====================
        // 关联容器 (Associative Containers)
        // ====================
        
        // 有序映射
        template<typename Key, typename Value, typename Compare = eastl::less<Key>, typename Allocator = eastl::allocator>
        using Map = eastl::map<Key, Value, Compare, Allocator>;
        
        // 有序集合
        template<typename T, typename Compare = eastl::less<T>, typename Allocator = eastl::allocator>
        using Set = eastl::set<T, Compare, Allocator>;
        
        // 无序映射（哈希表）
        template<typename Key, typename Value, typename Hash = eastl::hash<Key>, typename Predicate = eastl::equal_to<Key>, typename Allocator = eastl::allocator>
        using HashMap = eastl::unordered_map<Key, Value, Hash, Predicate, Allocator>;
        
        // 无序集合（哈希集合）
        template<typename T, typename Hash = eastl::hash<T>, typename Predicate = eastl::equal_to<T>, typename Allocator = eastl::allocator>
        using HashSet = eastl::unordered_set<T, Hash, Predicate, Allocator>;

        // 固定容量哈希映射（无运行期 rehash/动态分配；N=元素槽数，M=桶数）
        template<typename Key, typename Value, size_t N, size_t M, bool bEnableOverflow = false,
                 typename Hash = eastl::hash<Key>, typename Predicate = eastl::equal_to<Key>>
        using FixedHashMap = eastl::fixed_hash_map<Key, Value, N, M, bEnableOverflow, Hash, Predicate>;
        
        // ====================
        // 容器适配器 (Container Adapters)
        // ====================
        
        // 栈
        template<typename T, typename Container = Vector<T>>
        using Stack = eastl::stack<T, Container>;
        
        // 队列
        template<typename T, typename Container = Deque<T>>
        using Queue = eastl::queue<T, Container>;
        
        // 优先队列
        template<typename T, typename Container = Vector<T>, typename Compare = eastl::less<typename Container::value_type>>
        using PriorityQueue = eastl::priority_queue<T, Container, Compare>;
        
        // ====================
        // 字符串类型
        // ====================
        
        // 基础字符串类型
        using String = eastl::string;
        using WString = eastl::wstring;
        using String16 = eastl::u16string;
        using String32 = eastl::u32string;
        
        // 字符串视图（如果EASTL支持）
        #if EASTL_STRING_VIEW_ENABLED
        using StringView = eastl::string_view;
        using WStringView = eastl::wstring_view;
        #endif
        
        // ====================
        // 工具类型
        // ====================

        // 位集
        template<size_t N>
        using Bitset = eastl::bitset<N>;

        // 配对
        template<typename T1, typename T2>
        using Pair = eastl::pair<T1, T2>;

        // ====================
        // 常用函数别名
        // ====================

        // 创建配对的便捷函数
        template<typename T1, typename T2>
        TINA_CORE_API constexpr Pair<eastl::decay_t<T1>, eastl::decay_t<T2>> MakePair(T1&& first, T2&& second) {
            return eastl::make_pair(eastl::forward<T1>(first), eastl::forward<T2>(second));
        }
        
        // ====================
        // 算法函数别名
        // ====================
        
        // 查找算法
        template<typename Iterator, typename T>
        TINA_CORE_API Iterator Find(Iterator first, Iterator last, const T& value) {
            return eastl::find(first, last, value);
        }
        
        template<typename Iterator, typename Predicate>
        TINA_CORE_API Iterator FindIf(Iterator first, Iterator last, Predicate pred) {
            return eastl::find_if(first, last, pred);
        }
        
        // 排序算法
        template<typename Iterator>
        TINA_CORE_API void Sort(Iterator first, Iterator last) {
            eastl::sort(first, last);
        }
        
        template<typename Iterator, typename Compare>
        TINA_CORE_API void Sort(Iterator first, Iterator last, Compare comp) {
            eastl::sort(first, last, comp);
        }
        
        // 复制算法
        template<typename InputIterator, typename OutputIterator>
        TINA_CORE_API OutputIterator Copy(InputIterator first, InputIterator last, OutputIterator result) {
            return eastl::copy(first, last, result);
        }
        
        // 填充算法
        template<typename Iterator, typename T>
        TINA_CORE_API void Fill(Iterator first, Iterator last, const T& value) {
            eastl::fill(first, last, value);
        }
        
        // 计数算法
        template<typename Iterator, typename T>
        TINA_CORE_API typename eastl::iterator_traits<Iterator>::difference_type 
        Count(Iterator first, Iterator last, const T& value) {
            return eastl::count(first, last, value);
        }
        
        template<typename Iterator, typename Predicate>
        TINA_CORE_API typename eastl::iterator_traits<Iterator>::difference_type 
        CountIf(Iterator first, Iterator last, Predicate pred) {
            return eastl::count_if(first, last, pred);
        }
        
        // 移除算法
        template<typename Iterator, typename T>
        TINA_CORE_API Iterator Remove(Iterator first, Iterator last, const T& value) {
            return eastl::remove(first, last, value);
        }
        
        template<typename Iterator, typename Predicate>
        TINA_CORE_API Iterator RemoveIf(Iterator first, Iterator last, Predicate pred) {
            return eastl::remove_if(first, last, pred);
        }
        
        // 变换算法
        template<typename InputIterator, typename OutputIterator, typename UnaryOperation>
        TINA_CORE_API OutputIterator Transform(InputIterator first, InputIterator last, 
                                              OutputIterator result, UnaryOperation op) {
            return eastl::transform(first, last, result, op);
        }
        
        // 反转算法
        template<typename Iterator>
        TINA_CORE_API void Reverse(Iterator first, Iterator last) {
            eastl::reverse(first, last);
        }
        
        // 最小最大算法
        template<typename T>
        TINA_CORE_API constexpr const T& Min(const T& a, const T& b) {
            return eastl::min(a, b);
        }
        
        template<typename T>
        TINA_CORE_API constexpr const T& Max(const T& a, const T& b) {
            return eastl::max(a, b);
        }
        
        template<typename T, typename Compare>
        TINA_CORE_API constexpr const T& Min(const T& a, const T& b, Compare comp) {
            return eastl::min(a, b, comp);
        }
        
        template<typename T, typename Compare>
        TINA_CORE_API constexpr const T& Max(const T& a, const T& b, Compare comp) {
            return eastl::max(a, b, comp);
        }
        
        // ====================
        // 常用类型定义
        // ====================
        
        // 游戏开发中常用的容器类型别名
        template<typename T> using EntityList = Vector<T>;
        template<typename Key, typename Value> using EntityMap = HashMap<Key, Value>;
        template<typename T> using ComponentArray = Vector<T>;
        template<typename T> using ResourceCache = HashMap<String, T>;
        
        // 字符串相关
        using StringList = Vector<String>;
        using StringMap = HashMap<String, String>;
        
        // 数值类型列表
        using IntList = Vector<i32>;
        using FloatList = Vector<float>;
        using DoubleList = Vector<double>;

        // ====================
        // 固定容量容器 (Fixed Capacity Containers)
        // ====================

        // 固定容量向量（栈分配，无动态内存）
        template<typename T, size_t N, bool bEnableOverflow = true>
        using FixedVector = eastl::fixed_vector<T, N, bEnableOverflow>;

        // 环形缓冲区（适合队列操作）
        template<typename T, typename Container = Vector<T>>
        using RingBuffer = eastl::ring_buffer<T, Container>;

        // ====================
        // 函数对象类型
        // ====================

        // 固定大小函数对象（避免堆分配）
        template<int SIZE_IN_BYTES, typename R, typename... Args>
        using FixedFunction = eastl::fixed_function<SIZE_IN_BYTES, R(Args...)>;

        // ====================
        // 比较器和谓词
        // ====================

        // 比较器
        template<typename T>
        using Less = eastl::less<T>;

        template<typename T>
        using Greater = eastl::greater<T>;

        template<typename T>
        using EqualTo = eastl::equal_to<T>;

        template<typename T>
        using NotEqualTo = eastl::not_equal_to<T>;

        // ====================
        // 类型特性
        // ====================

        template<typename T>
        using RemoveReference = eastl::remove_reference<T>;

        template<typename T>
        using DecayType = eastl::decay_t<T>;

        // ====================
        // 工具函数扩展
        // ====================

        // 移动语义
        template<typename T>
        TINA_CORE_API constexpr typename eastl::remove_reference<T>::type&& Move(T&& t) noexcept {
            return eastl::move(t);
        }

        // 完美转发
        template<typename T>
        TINA_CORE_API constexpr T&& Forward(typename eastl::remove_reference<T>::type& t) noexcept {
            return eastl::forward<T>(t);
        }

        template<typename T>
        TINA_CORE_API constexpr T&& Forward(typename eastl::remove_reference<T>::type&& t) noexcept {
            return eastl::forward<T>(t);
        }

        // 交换
        template<typename T>
        TINA_CORE_API void Swap(T& a, T& b) {
            eastl::swap(a, b);
        }

    } // namespace Container
} // namespace Tina
