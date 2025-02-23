//
// Created by wuxianggujun on 2025/2/15.
//

#pragma once
#include <mutex>
#include <unordered_map>
#include <atomic>
#include "tina/core/Singleton.hpp"
#include <GLFW/glfw3.h>

namespace Tina
{
    class GlfwMemoryManager : public Singleton<GlfwMemoryManager>
    {
    public:
        friend class Singleton<GlfwMemoryManager>;

        void setupGlfwAllocator() {
            GLFWallocator allocator;
            allocator.allocate = allocate;
            allocator.reallocate = reallocate;
            allocator.deallocate = deallocate;
            allocator.user = nullptr;
            glfwInitAllocator(&allocator);
        }

        static void* allocate(size_t size, void* user);
        static void* reallocate(void* block, size_t size, void* user);
        static void deallocate(void* block, void* user);

        size_t getCurrentMemoryUsage() const { return s_currentAllocated; }
        size_t getPeakMemoryUsage() const { return s_peakAllocated; }
        size_t getTotalMemoryUsage() const { return s_totalAllocated; }

        static size_t getCurrentAllocated() { return s_currentAllocated; }
        static size_t getPeakAllocated() { return s_peakAllocated; }
        static size_t getTotalAllocated() { return s_totalAllocated; }

    private:
        GlfwMemoryManager() = default;
        ~GlfwMemoryManager() = default;

        static std::atomic<size_t> s_totalAllocated;
        static std::atomic<size_t> s_currentAllocated;
        static std::atomic<size_t> s_peakAllocated;
        static std::mutex s_mutex;

        struct MemoryBlock
        {
            size_t size;
            void* ptr;
        };

        static std::unordered_map<void*, MemoryBlock> s_allocations;
    };
} // namespace Tina
