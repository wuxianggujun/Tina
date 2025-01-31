#pragma once

#include <EASTL/vector.h>
#include <EASTL/allocator.h>


namespace tina::container {

template<typename T>
class Vector {
public:
    using Iterator = typename eastl::vector<T>::iterator;
    using ConstIterator = typename eastl::vector<T>::const_iterator;
    
    Vector() = default;
    Vector(const Vector&) = default;
    Vector(Vector&&) noexcept = default;
    
    explicit Vector(size_t size) : mVector(size) {}
    Vector(std::initializer_list<T> init) : mVector(init) {}
    
    Vector& operator=(const Vector&) = default;
    Vector& operator=(Vector&&) noexcept = default;
    
    // Element access
    T& operator[](size_t index) { return mVector[index]; }
    const T& operator[](size_t index) const { return mVector[index]; }
    
    T& at(size_t index) { return mVector.at(index); }
    const T& at(size_t index) const { return mVector.at(index); }
    
    T& front() { return mVector.front(); }
    const T& front() const { return mVector.front(); }
    
    T& back() { return mVector.back(); }
    const T& back() const { return mVector.back(); }
    
    // Iterators
    Iterator begin() { return mVector.begin(); }
    ConstIterator begin() const { return mVector.begin(); }
    Iterator end() { return mVector.end(); }
    ConstIterator end() const { return mVector.end(); }
    
    // Capacity
    bool empty() const { return mVector.empty(); }
    size_t size() const { return mVector.size(); }
    void reserve(size_t newCapacity) { mVector.reserve(newCapacity); }
    size_t capacity() const { return mVector.capacity(); }
    
    // Modifiers
    void clear() { mVector.clear(); }
    void push_back(const T& value) { mVector.push_back(value); }
    void push_back(T&& value) { mVector.push_back(std::move(value)); }
    void pop_back() { mVector.pop_back(); }
    
    template<typename... Args>
    void emplace_back(Args&&... args) {
        mVector.emplace_back(std::forward<Args>(args)...);
    }
    
    Iterator erase(ConstIterator pos) { return mVector.erase(pos); }
    Iterator erase(ConstIterator first, ConstIterator last) { return mVector.erase(first, last); }
    
    void resize(size_t newSize) { mVector.resize(newSize); }
    
private:
    eastl::vector<T> mVector;
};

} // namespace tina::container
