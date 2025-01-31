#include <EASTL/allocator.h>
#include <new>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
    #include <malloc.h>
    #define EASTL_ALIGNED_MALLOC(size, alignment) _aligned_malloc(size, alignment)
    #define EASTL_ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
    #define EASTL_ALIGNED_MALLOC(size, alignment) aligned_alloc(alignment, size)
    #define EASTL_ALIGNED_FREE(ptr) free(ptr)
#endif

///////////////////////////////////////////////////////////////////////////////
// EASTL Required Global Operators
// 这些操作符必须在全局命名空间中定义，因为：
// 1. EASTL在全局命名空间中查找它们
// 2. 它们是内存分配的基本操作，需要全局可见
// 3. C++标准要求内存分配操作符在全局命名空间中
///////////////////////////////////////////////////////////////////////////////

// 基本的内存分配
void* operator new[](size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// 对齐的内存分配
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    void* ptr = EASTL_ALIGNED_MALLOC(size, alignment);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// 单个对象的内存分配
void* operator new(size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// 对齐的单个对象内存分配
void* operator new(size_t size, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    void* ptr = EASTL_ALIGNED_MALLOC(size, alignment);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// 基本的内存释放
void operator delete(void* p, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    std::free(p);
}

void operator delete[](void* p, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    std::free(p);
}

// 对齐的内存释放
void operator delete(void* p, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    EASTL_ALIGNED_FREE(p);
}

void operator delete[](void* p, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    EASTL_ALIGNED_FREE(p);
}

// 标准的delete操作符（C++17要求）
void operator delete(void* ptr, std::size_t size) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    std::free(ptr);
} 