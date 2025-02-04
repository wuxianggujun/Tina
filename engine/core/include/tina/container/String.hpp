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
        // 构造函数
        String();
        String(const String& other);
        explicit String(const char* str);
        explicit String(const std::string& str);
        explicit String(std::string_view str);
        String(String&& other) noexcept;
        String(const char* str, size_t length);

        ~String();

        // 赋值运算符
        String& operator=(const String& other);
        String& operator=(String&& other) noexcept;

        // 类型转换
        explicit operator std::string() const;
        explicit operator std::string_view() const;

        // 访问方法
        [[nodiscard]] const char* c_str() const;
        [[nodiscard]] size_t length() const;
        [[nodiscard]] size_t size() const;
        [[nodiscard]] bool empty() const;
        [[nodiscard]] size_t capacity() const;

        // 操作符重载
        bool operator==(const String& other) const;
        bool operator==(const char* str) const;
        bool operator!=(const String& other) const;
        String& operator+=(const String& other);
        String& operator+=(const char* str);
        String operator+(const String& other) const;
        String operator+(const char* str) const;
        char& operator[](size_t index);
        const char& operator[](size_t index) const;

        // 修改方法
        void clear();
        void reserve(size_t capacity);
        void resize(size_t newSize, char ch = '\0');
        void shrink_to_fit();

        // 查找方法
        size_t find(const String& str, size_t pos = 0) const;
        size_t find(const char* str, size_t pos = 0) const;
        size_t rfind(const String& str, size_t pos = npos) const;
        size_t rfind(const char* str, size_t pos = npos) const;

        // 子串操作
        String substr(size_t pos = 0, size_t len = npos) const;
        void append(const char* str, size_t n);
        void append(const String& str);
        void insert(size_t pos, const String& str);
        void insert(size_t pos, const char* str);
        void erase(size_t pos = 0, size_t len = npos);
        void replace(size_t pos, size_t len, const String& str);
        void replace(size_t pos, size_t len, const char* str);

        static constexpr size_t SSO_CAPACITY = 15;
        static constexpr size_t npos = static_cast<size_t>(-1);

    private:

        union
        {
            struct
            {
                char* m_data;
                size_t m_capacity;
            } heap;

            struct
            {
                char data[SSO_CAPACITY + 1];
            } sso;
        };
        size_t m_size;
        bool m_isSSO; // 标记是否使用SSO

        // 私有辅助方法
        void checkIndex(size_t index) const;
        char* allocateMemory(size_t size);
        void deallocateMemory(char* ptr, size_t size);
        bool canUseSSO(size_t size) const { return size <= SSO_CAPACITY; }
        char* getDataPtr() { return m_isSSO ? sso.data : heap.m_data; }
        const char* getDataPtr() const { return m_isSSO ? sso.data : heap.m_data; }
        size_t getCapacity() const { return m_isSSO ? SSO_CAPACITY : heap.m_capacity; }
        void moveFrom(String&& other) noexcept;
        void copyFrom(const String& other);
        void switchToHeap(size_t newCapacity);
        void tryToSwitchToSSO();

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
        void updatePoolStats(PoolStats::PoolTypeStats& stats, bool isAllocation);

        void initializePool(std::vector<MemoryBlock>& pool, size_t blockSize);
        void releasePool(std::vector<MemoryBlock>& pool);
        char* allocateFromPool(std::vector<MemoryBlock>& pool, size_t size);
        void deallocateToPool(std::vector<MemoryBlock>& pool, const char* ptr);
    };
}

// fmt 格式化支持
template<>
struct fmt::formatter<Tina::String> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const Tina::String& str, FormatContext& ctx) const {
        return formatter<string_view>::format(string_view(str.c_str(), str.length()), ctx);
    }
};

// 在 String.hpp 文件末尾修改格式化器实现
template <>
struct fmt::formatter<Tina::StringMemoryPool::PoolStats::PoolTypeStats>
{
    // 保持 parse 方法不变
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // 修改 format 方法，添加 const 限定符
    template <typename FormatContext>
    auto format(const Tina::StringMemoryPool::PoolStats::PoolTypeStats& stats, FormatContext& ctx) const
    {
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