//
// Created by wuxianggujun on 2025/2/15.
//

#pragma once
#include <mutex>
#include <unordered_map>

namespace Tina
{
    class GlfwMemoryManager
    {
    public:
        static void* allocate(size_t size, void* user);
        static void* reallocate(void* block, size_t size, void* user);
        static void deallocate(void* block, void* user);

        static size_t getTotalAllocated() { return s_totalAllocated; }
        static size_t getCurrentAllocated() { return s_currentAllocated; }
        static size_t getPeakAllocated() { return s_peakAllocated; }

    private:
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
} // Tina
