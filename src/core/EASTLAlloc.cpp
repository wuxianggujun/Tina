//
// EASTL 调试分配接口桥接
// 说明：当启用 EASTL 的带调试参数分配路径时（默认 allocator::allocate 会调用
// operator new/new[] 带 (name, flags, debugFlags, file, line) 重载），
// 需要由宿主程序提供这些全局 new/delete 重载以完成链接。
// 这里将其统一映射到标准 new/delete 与平台对齐分配实现。
//

#include <new>
#include <cstddef>
#include <cstdlib>
#include <EASTL/fixed_hash_set.h>
#include <mutex>

#if defined(_MSC_VER)
#   include <malloc.h>
#endif

// 记录通过“对齐分配重载”分配出来的指针，用于在缺少对齐信息的 delete[] 路径上做正确释放
namespace {
    // 防重入标记：防止在 erase 带来的节点释放过程中再次进入 untrack_aligned，造成死锁/递归
    thread_local bool s_in_untrack = false;
    // 防重入标记：防止在 untrack_aligned 执行期间，其他路径触发的 aligned 分配进入 track_aligned，
    // 导致在同一线程内对跟踪集合进行嵌套修改（insert/erase），从而引发未定义行为或调试断点。
    thread_local bool s_in_track = false;
    // 说明：使用“永不析构”的静态堆对象，避免在进程退出阶段因静态析构次序导致
    // untrack_aligned 在容器析构过程中再次访问容器而触发未定义行为。
    eastl::fixed_hash_set<void*, 65536, 65536, false>& aligned_set()
    {
        static eastl::fixed_hash_set<void*, 65536, 65536, false> s; // 固定容量，避免跟踪集合自身分配
        return s;
    }
    std::mutex& aligned_set_mutex()
    {
        // ✅ 使用 Meyer's Singleton，自动管理生命周期
        static std::mutex m;
        return m;
    }
    inline void track_aligned(void* p)
    {
        if (!p) return;
        // 若当前正处于 untrack 阶段或已在 track 内部，则不要再次尝试跟踪，
        // 以避免对 aligned_set 进行嵌套修改（insert 过程中容器可能分配/释放自身内存）。
        if (s_in_untrack || s_in_track) return;
        struct Guard { Guard(){ s_in_track = true; } ~Guard(){ s_in_track = false; } } _guard;
        std::lock_guard<std::mutex> _g(aligned_set_mutex());
        aligned_set().insert(p);
    }
    inline bool untrack_aligned(void* p)
    {
        if (!p) return false;
        if (s_in_untrack) return false; // 避免在容器节点释放时重入自身
        struct Guard { Guard(){ s_in_untrack = true; } ~Guard(){ s_in_untrack = false; } } _guard;
        std::lock_guard<std::mutex> _g(aligned_set_mutex());
        auto it = aligned_set().find(p);
        if (it != aligned_set().end()) { aligned_set().erase(it); return true; }
        return false;
    }
}

// The translation unit replaces global delete overloads because EASTL always
// deallocates through delete[]. Replace the matching allocation side as well;
// otherwise ordinary std::allocator allocations use the runtime operator new
// and are later released by our free-based delete, which is undefined behavior.
void* operator new(std::size_t size)
{
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    void* memory = nullptr;
#if defined(_MSC_VER)
    memory = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&memory,
                       static_cast<std::size_t>(alignment),
                       size == 0 ? 1 : size) != 0) {
        memory = nullptr;
    }
#endif
    if (!memory) {
        throw std::bad_alloc();
    }
    track_aligned(memory);
    return memory;
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void* operator new(std::size_t size,
                   std::align_val_t alignment,
                   const std::nothrow_t&) noexcept
{
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size,
                     std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    try {
        return ::operator new[](size, alignment);
    } catch (...) {
        return nullptr;
    }
}

// 非对齐分配（对象 & 数组）
void* operator new(std::size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return ::operator new(size);
}

void* operator new[](std::size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    // 使用标量 new，确保与后续调用的非数组 delete 对应
    return ::operator new(size);
}

// 非对齐对应的 placement delete（异常路径匹配调用），必须提供以满足链接
void operator delete(void* p, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}

void operator delete[](void* p, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}

// C++14 尺寸感知 delete（可能被 EASTL/编译器选用）
void operator delete(void* p, std::size_t) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}
void operator delete[](void* p, std::size_t) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}

// 带调试参数的尺寸感知 delete（EASTL 可能调用）
void operator delete(void* p, std::size_t, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}
void operator delete[](void* p, std::size_t, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}

// 对齐分配（对象 & 数组）
void* operator new(std::size_t size, std::size_t alignment, std::size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
#if defined(_MSC_VER)
    void* p = _aligned_malloc(size, alignment);
    if (!p) throw std::bad_alloc();
    track_aligned(p);
    return p;
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0)
        p = nullptr;
    if (!p) throw std::bad_alloc();
    track_aligned(p);
    return p;
#endif
}

void* operator new[](std::size_t size, std::size_t alignment, std::size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
#if defined(_MSC_VER)
    void* p = _aligned_malloc(size, alignment);
    if (!p) throw std::bad_alloc();
    track_aligned(p);
    return p;
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0)
        p = nullptr;
    if (!p) throw std::bad_alloc();
    track_aligned(p);
    return p;
#endif
}

// 对齐分配对应的 placement delete（异常路径匹配调用）
void operator delete(void* p, std::size_t /*alignment*/, std::size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}

void operator delete[](void* p, std::size_t /*alignment*/, std::size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}

// C++17 对齐 delete（编译器可能选择这些重载）
void operator delete(void* p, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}
void operator delete(void* p, std::size_t /*sz*/, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}
void operator delete[](void* p, std::size_t /*sz*/, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    (void)untrack_aligned(p);
    _aligned_free(p);
#else
    (void)untrack_aligned(p);
    std::free(p);
#endif
}

// 关键：eastl::allocator::deallocate 对于非 DLL 情况总是调用 delete[](char*)p。
// 这里覆盖无参的 delete[]，在不带对齐信息时根据我们记录的集合选择正确的释放方法，避免 _aligned_malloc/free 与 free 混用。
void operator delete[](void* p) noexcept
{
    if (!p) return;
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}

void operator delete(void* p) noexcept
{
    if (!p) return;
#if defined(_MSC_VER)
    if (untrack_aligned(p)) { _aligned_free(p); return; }
    std::free(p);
#else
    if (untrack_aligned(p)) { std::free(p); return; }
    std::free(p);
#endif
}
