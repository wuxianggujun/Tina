//
// Created by wuxianggujun on 2025/1/26.
//

#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <vector>
#include <fmt/base.h>

namespace Tina
{
    class StringMemoryPool;

    class String
    {
    public:
        String();
        String(const String& other);
        explicit String(const char* str);
        explicit String(const std::string& str);
        explicit String(std::string_view str);
        String(String&& other) noexcept;

        ~String();

        String& operator=(const String& other);
        String& operator=(String&& other) noexcept;

        explicit operator std::string() const;
        explicit operator std::string_view() const;

        [[nodiscard]] const char* c_str() const;
        [[nodiscard]] size_t length() const;
        [[nodiscard]] size_t size() const;
        [[nodiscard]] bool empty() const;
        [[nodiscard]] size_t capacity() const;

        bool operator==(const String& other) const;
        bool operator==(const char* str) const;
        bool operator!=(const String& other) const;
        String& operator+=(const String& other);
        String& operator+=(const char* str);
        String operator+(const String& other) const;
        String operator+(const char* str) const;
        char& operator[](size_t index);
        const char& operator[](size_t index) const;

        void clear();
        void reserve(size_t capacity);

    private:
        void checkIndex(size_t index) const;
        char* allocateMemory(size_t size);
        void deallocateMemory(char* ptr, size_t size);

        char* m_data;
        size_t m_size;
        size_t m_capacity;
        static StringMemoryPool& getMemoryPool();
    };

    inline String operator+(const char* str, const String& other);


    class StringMemoryPool
    {
    public:

        struct PoolStats
        {
            size_t totalAllocations{0};
            size_t currentAllocations{0};
            size_t peakAllocations{0};
            size_t totalMemoryUsed{0};
            size_t currentMemoryUsed{0};
            size_t peakMemoryUsed{0};

            struct PoolTypeStats
            {
                size_t allocations{0};
                size_t deallocations{0};
                size_t currentUsage{0};
                size_t peakUsage{0};
            };

            PoolTypeStats smallPool;
            PoolTypeStats mediumPool;
            PoolTypeStats largePool;
            PoolTypeStats customAllocations;
        };

        static constexpr size_t SMALL_STRING_SIZE = 32;
        static constexpr size_t MEDIUM_STRING_SIZE = 128;
        static constexpr size_t LARGE_STRING_SIZE = 512;
        static constexpr size_t BLOCK_COUNT = 1024;

        struct MemoryBlock
        {
            char* data;
            bool used;
            size_t size;
        };

        StringMemoryPool();
        ~StringMemoryPool();

        char* allocate(size_t size);
        void deallocate(char* ptr, size_t size);
        static StringMemoryPool& getInstance();

        const PoolStats& getStats() const;

        void resetStats();
        void printStats() const;

    private:
        std::vector<MemoryBlock> m_blocks;
        std::vector<MemoryBlock> m_mediumPool;
        std::vector<MemoryBlock> m_largePool;

        PoolStats m_stats;

        void updateAllocationStats(size_t size);
        void updateDeallocationStats(size_t size);
        void updatePoolStats(PoolStats::PoolTypeStats& stats,bool isAllocation);

        void initializePool(std::vector<MemoryBlock>& pool, size_t blockSize);
        void releasePool(std::vector<MemoryBlock>& pool);
        char* allocateFromPool(std::vector<MemoryBlock>& pool, size_t size);
        void deallocateToPool(std::vector<MemoryBlock>& pool, const char* ptr);
    };
}

// 在 String.hpp 文件末尾修改格式化器实现
template<>
struct fmt::formatter<Tina::StringMemoryPool::PoolStats::PoolTypeStats> {
    // 保持 parse 方法不变
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // 修改 format 方法，添加 const 限定符
    template<typename FormatContext>
    auto format(const Tina::StringMemoryPool::PoolStats::PoolTypeStats& stats, FormatContext& ctx) const {
        return fmt::format_to(
            ctx.out(),
            "allocations: {}, deallocations: {}, current_usage: {}, peak_usage: {}",
            stats.allocations,
            stats.deallocations,
            stats.currentUsage,
            stats.peakUsage
        );
    }
};
