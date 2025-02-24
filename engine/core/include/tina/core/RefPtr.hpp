#pragma once

#include "tina/core/Core.hpp"
#include "tina/log/Log.hpp"

namespace Tina {

/**
 * @brief 引用计数智能指针
 * 
 * 提供自动引用计数管理的智能指针。
 */
template<typename T>
class RefPtr {
public:
    /**
     * @brief 默认构造函数
     */
    RefPtr() : m_ptr(nullptr) {}

    /**
     * @brief 从原始指针构造
     * @param ptr 原始指针
     */
    explicit RefPtr(T* ptr) : m_ptr(ptr) {
        if (m_ptr) {
            m_ptr->addRef();
            TINA_ENGINE_DEBUG("RefPtr constructed with ptr: {}, refCount: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
        }
    }

    /**
     * @brief 拷贝构造函数
     */
    RefPtr(const RefPtr& other) : m_ptr(other.m_ptr) {
        if (m_ptr) {
            m_ptr->addRef();
            TINA_ENGINE_DEBUG("RefPtr copy constructed, ptr: {}, refCount: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
        }
    }

    /**
     * @brief 转换构造函数,用于继承类型转换
     */
    template<typename U>
    RefPtr(const RefPtr<U>& other) : m_ptr(dynamic_cast<T*>(other.get())) {
        if (m_ptr) {
            m_ptr->addRef();
            TINA_ENGINE_DEBUG("RefPtr conversion constructed, ptr: {}, refCount: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
        }
    }

    /**
     * @brief 移动构造函数
     */
    RefPtr(RefPtr&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
        if (m_ptr) {
            TINA_ENGINE_DEBUG("RefPtr move constructed, ptr: {}, refCount: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
        }
    }

    /**
     * @brief 析构函数
     */
    ~RefPtr() {
        if (m_ptr) {
            TINA_ENGINE_DEBUG("RefPtr destructing, ptr: {}, refCount before release: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
            m_ptr->release();
        }
    }

    /**
     * @brief 拷贝赋值运算符
     */
    RefPtr& operator=(const RefPtr& other) {
        if (this != &other) {
            if (m_ptr) {
                TINA_ENGINE_DEBUG("RefPtr copy assignment releasing old ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
                m_ptr->release();
            }
            m_ptr = other.m_ptr;
            if (m_ptr) {
                m_ptr->addRef();
                TINA_ENGINE_DEBUG("RefPtr copy assignment with new ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
            }
        }
        return *this;
    }

    /**
     * @brief 转换赋值运算符,用于继承类型转换
     */
    template<typename U>
    RefPtr& operator=(const RefPtr<U>& other) {
        if ((void*)this != (void*)&other) {
            if (m_ptr) {
                TINA_ENGINE_DEBUG("RefPtr conversion assignment releasing old ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
                m_ptr->release();
            }
            m_ptr = dynamic_cast<T*>(other.get());
            if (m_ptr) {
                m_ptr->addRef();
                TINA_ENGINE_DEBUG("RefPtr conversion assignment with new ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
            }
        }
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     */
    RefPtr& operator=(RefPtr&& other) noexcept {
        if (this != &other) {
            if (m_ptr) {
                TINA_ENGINE_DEBUG("RefPtr move assignment releasing old ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
                m_ptr->release();
            }
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            if (m_ptr) {
                TINA_ENGINE_DEBUG("RefPtr move assignment with new ptr: {}, refCount: {}", 
                    (void*)m_ptr, m_ptr->getRefCount());
            }
        }
        return *this;
    }

    /**
     * @brief 重置指针
     */
    void reset() {
        if (m_ptr) {
            TINA_ENGINE_DEBUG("RefPtr reset releasing ptr: {}, refCount: {}", 
                (void*)m_ptr, m_ptr->getRefCount());
            m_ptr->release();
            m_ptr = nullptr;
        }
    }

    /**
     * @brief 获取原始指针
     */
    T* get() const { return m_ptr; }

    /**
     * @brief 解引用操作符
     */
    T& operator*() const { return *m_ptr; }

    /**
     * @brief 箭头操作符
     */
    T* operator->() const { return m_ptr; }

    /**
     * @brief 布尔转换操作符
     */
    explicit operator bool() const { return m_ptr != nullptr; }

    /**
     * @brief 创建空RefPtr的静态方法
     */
    template<typename U = T>
    static RefPtr<U> null() { return RefPtr<U>(); }

private:
    T* m_ptr;  ///< 原始指针

    template<typename U> friend class RefPtr;
};

/**
 * @brief 静态类型转换
 * @param ptr 源智能指针
 * @return 转换后的智能指针
 */
template<typename T, typename U>
RefPtr<T> static_pointer_cast(const RefPtr<U>& ptr) {
    return RefPtr<T>(static_cast<T*>(ptr.get()));
}

/**
 * @brief 动态类型转换
 * @param ptr 源智能指针
 * @return 转换后的智能指针
 */
template<typename T, typename U>
RefPtr<T> dynamic_pointer_cast(const RefPtr<U>& ptr) {
    return RefPtr<T>(dynamic_cast<T*>(ptr.get()));
}

} // namespace Tina 