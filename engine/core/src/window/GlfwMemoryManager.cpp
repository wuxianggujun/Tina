//
// Created by wuxianggujun on 2025/2/15.
//

#include "tina/window/GlfwMemoryManager.hpp"
#include "tina/log/Logger.hpp"

namespace Tina
{
    std::atomic<size_t> GlfwMemoryManager::s_totalAllocated(0);
    std::atomic<size_t> GlfwMemoryManager::s_currentAllocated(0);
    std::atomic<size_t> GlfwMemoryManager::s_peakAllocated(0);
    std::mutex GlfwMemoryManager::s_mutex;
    std::unordered_map<void*, GlfwMemoryManager::MemoryBlock> GlfwMemoryManager::s_allocations;

    void* GlfwMemoryManager::allocate(size_t size, void* user)
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        // 分配内存
        void* ptr = _aligned_malloc(size, 16);
        if (!ptr)
        {
            TINA_LOG_ERROR("Glfw memory allocation failed,size:{}", size);
            return nullptr;
        }
        // 更新统计
        s_totalAllocated += size;
        s_currentAllocated += size;
        s_peakAllocated = std::max(s_peakAllocated.load(), s_currentAllocated.load());

        // 记录分配
        s_allocations[ptr] = {size, ptr};

        // 使用bytes来显示更精确的数值
        TINA_LOG_DEBUG("GLFW allocated {}bytes, total: {}bytes, current: {}bytes",
            size,
            s_totalAllocated.load(),
            s_currentAllocated.load());

        return ptr;
    }

    void* GlfwMemoryManager::reallocate(void* block, size_t size, void* user)
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (!block)
        {
            return allocate(size, user);
        }

        // 获取原始大小

        auto it = s_allocations.find(block);
        if (it == s_allocations.end())
        {
            TINA_LOG_ERROR("GLFW realloc failed: block not found");
            return nullptr;
        }

        size_t oldSize = it->second.size;
        // 重新分配内存
        void* newPtr = _aligned_realloc(block, size, 16);
        if (!newPtr)
        {
            TINA_LOG_ERROR("GLFW memory reallocation failed, size: {}", size);
            return nullptr;
        }

        // 更新统计
        s_currentAllocated = s_currentAllocated - oldSize + size;
        s_peakAllocated = std::max(s_peakAllocated.load(), s_currentAllocated.load());
        // 更新记录
        s_allocations.erase(block);
        s_allocations[newPtr] = {size, newPtr};

        TINA_LOG_DEBUG("GLFW reallocated {} -> {}bytes, total: {}MB, current: {}MB",
                       oldSize, size,
                       s_totalAllocated.load() / (1024*1024),
                       s_currentAllocated.load() / (1024*1024));

        return newPtr;
    }

    void GlfwMemoryManager::deallocate(void* block, void* user)
    {
        if (!block) return;

        std::lock_guard<std::mutex> lock(s_mutex);

        // 获取大小
        auto it = s_allocations.find(block);
        if (it == s_allocations.end())
        {
            TINA_LOG_ERROR("GLFW dealloc failed: block not found");
            return;
        }

        size_t size = it->second.size;
        // 释放内存
        _aligned_free(block);
        // 更新统计
        s_currentAllocated -= size;
        // 删除记录
        s_allocations.erase(it);

        TINA_LOG_DEBUG("GLFW deallocated {}bytes, total: {}bytes, current: {}bytes",
            size,
            s_totalAllocated.load(),
            s_currentAllocated.load());
    }
} // Tina
