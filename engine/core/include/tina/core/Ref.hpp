#pragma once

#include "tina/core/Core.hpp"
#include <atomic>
#include <cassert>
#include <memory>

namespace Tina {

/**
 * @brief 引用计数基类
 * 
 * 为需要生命周期管理的对象提供基础设施。
 * 提供引用计数支持，用于管理系统资源或被多处引用的长生命周期对象。
 * 内置的引用计数消除了手动跟踪对象所有权的需求。
 * 
 * 注意：推荐使用智能指针(std::shared_ptr, std::unique_ptr)，
 * 除非有特殊的性能要求或需要更细粒度的控制。
 */
class TINA_CORE_API Ref {
public:
    /**
     * @brief 增加引用计数
     * 
     * 调用者获得对象的引用时必须调用此方法。
     * 当不再需要对象时，必须调用release()来减少引用计数。
     */
    void addRef() {
        assert(m_refCount > 0 && "Invalid reference count");
        ++m_refCount;
    }

    /**
     * @brief 减少引用计数
     * 
     * 当引用计数降至0时，对象会被自动删除。
     * 对象创建时引用计数初始化为1。
     */
    void release() {
        assert(m_refCount > 0 && "Invalid reference count");
        if (--m_refCount == 0) {
            delete this;
        }
    }

    /**
     * @brief 获取当前引用计数
     * @return 当前引用计数
     */
    [[nodiscard]] uint32_t getRefCount() const {
        return m_refCount;
    }

protected:
    /** @brief 构造函数，初始引用计数为1 */
    Ref() : m_refCount(1) {}
    
    /** @brief 拷贝构造函数，新对象引用计数为1 */
    Ref(const Ref&) : m_refCount(1) {}
    
    /** @brief 虚析构函数 */
    virtual ~Ref() = default;

    /** @brief 赋值操作符，不影响引用计数 */
    Ref& operator=(const Ref&) { return *this; }

private:
    std::atomic<uint32_t> m_refCount;  // 使用原子类型确保线程安全
};

/**
 * @brief Ref对象的智能指针包装
 * 
 * 提供RAII风格的Ref对象管理，自动处理引用计数。
 * 使用示例：
 * @code
 * class MyClass : public Ref {};
 * RefPtr<MyClass> ptr = new MyClass();  // 自动管理引用计数
 * @endcode
 */
template<typename T>
class RefPtr {
public:
    RefPtr() : m_ptr(nullptr) {}
    
    RefPtr(T* ptr) : m_ptr(ptr) {}
    
    RefPtr(const RefPtr& other) : m_ptr(other.m_ptr) {
        if (m_ptr) m_ptr->addRef();
    }
    
    template<typename U>
    RefPtr(const RefPtr<U>& other) : m_ptr(dynamic_cast<T*>(other.get())) {
        if (m_ptr) m_ptr->addRef();
    }
    
    RefPtr(RefPtr&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }
    
    template<typename U>
    RefPtr(RefPtr<U>&& other) noexcept : m_ptr(dynamic_cast<T*>(other.get())) {
        if (m_ptr == other.get()) {
            other.release();
        } else if (m_ptr) {
            m_ptr->addRef();
        }
    }
    
    ~RefPtr() {
        if (m_ptr) m_ptr->release();
    }
    
    RefPtr& operator=(const RefPtr& other) {
        if (this != &other) {
            if (m_ptr) m_ptr->release();
            m_ptr = other.m_ptr;
            if (m_ptr) m_ptr->addRef();
        }
        return *this;
    }
    
    template<typename U>
    RefPtr& operator=(const RefPtr<U>& other) {
        T* ptr = dynamic_cast<T*>(other.get());
        if (m_ptr != ptr) {
            if (m_ptr) m_ptr->release();
            m_ptr = ptr;
            if (m_ptr) m_ptr->addRef();
        }
        return *this;
    }
    
    RefPtr& operator=(RefPtr&& other) noexcept {
        if (this != &other) {
            if (m_ptr) m_ptr->release();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }
    
    template<typename U>
    RefPtr& operator=(RefPtr<U>&& other) noexcept {
        T* ptr = dynamic_cast<T*>(other.get());
        if (m_ptr != ptr) {
            if (m_ptr) m_ptr->release();
            m_ptr = ptr;
            if (ptr == other.get()) {
                other.release();
            } else if (m_ptr) {
                m_ptr->addRef();
            }
        }
        return *this;
    }
    
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    operator bool() const { return m_ptr != nullptr; }
    
    void reset(T* ptr = nullptr) {
        if (m_ptr) m_ptr->release();
        m_ptr = ptr;
    }
    
    T* release() {
        T* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    template<typename U>
    friend class RefPtr;

private:
    T* m_ptr;
};

template<typename To, typename From>
RefPtr<To> static_pointer_cast(const RefPtr<From>& from) {
    return RefPtr<To>(static_cast<To*>(from.get()));
}

} // namespace Tina 