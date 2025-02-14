#pragma once

#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
    #include <tracy/TracyC.h>
#endif

namespace Tina {

class Profiler {
public:
    // 禁用拷贝和赋值
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    static Profiler& getInstance() {
        static Profiler instance;
        return instance;
    }

    // 线程命名
    static void setThreadName(const char* name) {
        #ifdef TRACY_ENABLE
            tracy::SetThreadName(name);
        #endif
    }

    // 内存分配跟踪
    static void* allocateMemory(size_t size) {
        #ifdef TRACY_ENABLE
            void* ptr = malloc(size);
            TracyCAlloc(ptr, size);
            return ptr;
        #else
            return malloc(size);
        #endif
    }

    static void freeMemory(void* ptr) {
        #ifdef TRACY_ENABLE
            TracyCFree(ptr);
            free(ptr);
        #else
            free(ptr);
        #endif
    }

    // 消息日志
    static void log(const char* message) {
        #ifdef TRACY_ENABLE
            TracyCMessage(message, strlen(message));
        #endif
    }

    // 绘图数据
    static void plotValue(const char* name, double value) {
        #ifdef TRACY_ENABLE
            TracyCPlot(name, value);
        #endif
    }

private:
    Profiler() = default;
    ~Profiler() = default;
};

} // namespace Tina

// 便捷宏
#ifdef TRACY_ENABLE
    #define TINA_PROFILE_FUNCTION() ZoneScoped
    #define TINA_PROFILE_SCOPE(name) ZoneScopedN(name)
    #define TINA_PROFILE_FRAME() FrameMark
    #define TINA_PROFILE_PLOT(name, value) Tina::Profiler::plotValue(name, value)
    #define TINA_PROFILE_LOG(message) Tina::Profiler::log(message)
    #define TINA_PROFILE_THREAD(name) Tina::Profiler::setThreadName(name)
    #define TINA_NEW(type, ...) new (Tina::Profiler::allocateMemory(sizeof(type))) type(__VA_ARGS__)
    #define TINA_DELETE(ptr) do { if(ptr) { ptr->~type(); Tina::Profiler::freeMemory(ptr); ptr = nullptr; } } while(0)
#else
    #define TINA_PROFILE_FUNCTION()
    #define TINA_PROFILE_SCOPE(name)
    #define TINA_PROFILE_FRAME()
    #define TINA_PROFILE_PLOT(name, value)
    #define TINA_PROFILE_LOG(message)
    #define TINA_PROFILE_THREAD(name)
    #define TINA_NEW(type, ...) new type(__VA_ARGS__)
    #define TINA_DELETE(ptr) delete ptr
#endif

// 辅助宏
#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y) 