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
/*
namespace Tina
{
    class String
    {
    public:
        String(): m_data(nullptr), m_size(0), m_capacity(0)
        {
        }

        // 拷贝构造函数 - 不需要 explicit
        String(const String& other)
        {
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            if (other.m_data)
            {
                m_data = new char[m_capacity];
                std::memcpy(m_data, other.m_data, m_size + 1);
            }
            else
            {
                m_data = nullptr;
            }
        }

        explicit String(const char* str)
        {
            assert(str != nullptr && R"(Input String cannot be null)");
            m_size = std::strlen(str);
            m_capacity = m_size + 1;
            m_data = new char[m_capacity];
            std::memcpy(m_data, str, m_size + 1);
        }


        // Copy the assignment operator
        String& operator=(const String& other)
        {
            if (this != &other)
            {
                delete[] m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                if (other.m_data)
                {
                    m_data = new char[m_capacity];
                    std::memcpy(m_data, other.m_data, m_size + 1);
                }
                else
                {
                    m_data = nullptr;
                }
            }
            return *this;
        }

        // Move the assignment operator
        String& operator=(String&& other) noexcept
        {
            if (this != &other)
            {
                delete[] m_data;
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_data = nullptr;
                other.m_size = other.m_capacity = 0;
            }
            return *this;
        }




        bool operator==(const String& other) const
        {
            return m_size == other.m_size && std::memcmp(m_data, other.m_data, m_size) == 0;
        }

        bool operator==(const char* str) const
        {
            if (!str) return m_size == 0;
            return std::strcmp(m_data ? m_data : "", str) == 0;
        }

        // 添加与C字符串相关的操作
        String& operator+=(const char* str)
        {
            if (str)
            {
                size_t strLen = std::strlen(str);
                if (strLen > 0)
                {
                    size_t newSize = m_size + strLen;
                    if (newSize + 1 > m_capacity)
                    {
                        reserve(std::max(newSize + 1, m_capacity * 2));
                    }
                    std::memcpy(m_data + m_size, str, strLen + 1);
                    m_size = newSize;
                }
            }
            return *this;
        }

        // 添加 String 类型的 += 操作符
        String& operator+=(const String& other)
        {
            if (!other.empty())
            {
                size_t newSize = m_size + other.m_size;
                if (newSize + 1 > m_capacity)
                {
                    reserve(std::max(newSize + 1, m_capacity * 2));
                }
                if (other.m_data)
                {
                    std::memcpy(m_data + m_size, other.m_data, other.m_size + 1);
                }
                m_size = newSize;
            }
            return *this;
        }

        String operator+(const String& other) const
        {
            String result(*this);
            result += other;
            return result;
        }

        String operator+(const char* str) const
        {
            String result(*this);
            result += str;
            return result;
        }

        bool operator!=(const String& other) const
        {
            return !(*this == other);
        }

        [[nodiscard]] size_t capacity() const
        {
            return m_capacity;
        }

        void clear()
        {
            delete[] m_data;
            m_data = nullptr;
            m_size = m_capacity = 0;
        }

        void reserve(size_t capacity)
        {
            if (capacity <= m_capacity) return; // 只在需要更大容量时重新分配

            char* newData = new char[capacity];
            if (m_data)
            {
                std::memcpy(newData, m_data, m_size + 1); // 包含空终止符
                delete[] m_data;
            }
            else
            {
                newData[0] = '\0'; // 确保新分配的内存有终止符
            }
            m_data = newData;
            m_capacity = capacity;
        }

        char& operator[](const size_t index)
        {
            checkIndex(index);
            return m_data[index];
        }

        const char& operator[](const size_t index) const
        {
            checkIndex(index);
            return m_data[index];
        }

    private:
        char* m_data;
        size_t m_size;
        size_t m_capacity;

        void checkIndex(size_t index) const
        {
            if (index >= m_size)
            {
                throw std::out_of_range("String index out of range");
            }
        }
    };

    inline String operator+(const char* str, const String& other)
    {
        return String(str) + other;
    }
} // Tina
*/

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

    private:
        std::vector<MemoryBlock> m_blocks;
        std::vector<MemoryBlock> m_mediumPool;
        std::vector<MemoryBlock> m_largePool;

        void initializePool(std::vector<MemoryBlock>& pool, size_t blockSize);
        void releasePool(std::vector<MemoryBlock>& pool);
        char* allocateFromPool(std::vector<MemoryBlock>& pool, size_t size);
        void deallocateToPool(std::vector<MemoryBlock>& pool, const char* ptr);
    };
}
