//
// Created by wuxianggujun on 2025/1/26.
//

#include "String.hpp"

namespace Tina
{
    char* String::allocateMemory(size_t size)
    {
        return getMemoryPool().allocate(size);
    }

    void String::deallocateMemory(char* ptr, size_t size)
    {
        getMemoryPool().deallocate(ptr, size);
    }

    StringMemoryPool& String::getMemoryPool()
    {
        return StringMemoryPool::getInstance();
    }

    StringMemoryPool::StringMemoryPool()
    {
        initializePool(m_blocks, SMALL_STRING_SIZE);
        initializePool(m_mediumPool, MEDIUM_STRING_SIZE);
        initializePool(m_largePool, LARGE_STRING_SIZE);
    }

    StringMemoryPool::~StringMemoryPool()
    {
        releasePool(m_blocks);
        releasePool(m_mediumPool);
        releasePool(m_largePool);
    }

    void StringMemoryPool::initializePool(std::vector<MemoryBlock>& pool, size_t blockSize)
    {
        pool.resize(BLOCK_COUNT);
        for (auto& block : pool)
        {
            block.data = new char[blockSize];
            block.used = false;
            block.size = blockSize;
        }
    }

    void StringMemoryPool::releasePool(std::vector<MemoryBlock>& pool)
    {
        for (auto& block : pool)
        {
            delete[] block.data;
        }
        pool.clear();
    }

    char* StringMemoryPool::allocate(size_t size)
    {
        if (size <= SMALL_STRING_SIZE)
        {
            return allocateFromPool(m_blocks, size);
        }

        if (size <= MEDIUM_STRING_SIZE)
        {
            return allocateFromPool(m_mediumPool, size);
        }

        if (size <= LARGE_STRING_SIZE)
        {
            return allocateFromPool(m_largePool, size);
        }
        return new char[size];
    }

    void StringMemoryPool::deallocate(char* ptr, size_t size)
    {
        if (!ptr) return;

        if (size <= SMALL_STRING_SIZE)
        {
            deallocateToPool(m_blocks, ptr);
        }
        else if (size <= MEDIUM_STRING_SIZE)
        {
            deallocateToPool(m_mediumPool, ptr);
        }
        else if (size <= LARGE_STRING_SIZE)
        {
            deallocateToPool(m_largePool, ptr);
        }
        else
        {
            delete[] ptr;
        }
    }

    char* StringMemoryPool::allocateFromPool(std::vector<MemoryBlock>& pool, size_t size)
    {
        for (auto& block : pool)
        {
            if (!block.used)
            {
                block.used = true;
                return block.data;
            }
        }

        MemoryBlock newBlock;
        newBlock.data = new char[size];
        newBlock.used = true;
        newBlock.size = size;
        pool.push_back(newBlock);
        return newBlock.data;
    }

    void StringMemoryPool::deallocateToPool(std::vector<MemoryBlock>& pool, const char* ptr)
    {
        for (auto& block : pool)
        {
            if (block.data == ptr)
            {
                block.used = false;
                return;
            }
        }
    }

    StringMemoryPool& StringMemoryPool::getInstance()
    {
        static StringMemoryPool instance;
        return instance;
    }


    String::String(): m_data(nullptr), m_size(0), m_capacity(0)
    {
    }

    String::String(const String& other)
    {
        m_size = other.m_size;
        m_capacity = other.m_capacity;
        if (other.m_data)
        {
            m_data = allocateMemory(m_capacity);
            std::memcpy(m_data, other.m_data, m_size + 1);
        }
        else
        {
            m_data = nullptr;
        }
    }

    String::String(const char* str)
    {
        assert(str != nullptr && "Input String cannot be null");
        m_size = std::strlen(str);
        m_capacity = m_size + 1;
        m_data = allocateMemory(m_capacity);
        std::memcpy(m_data, str, m_size + 1);
    }

    String::String(const std::string& str): String(str.c_str())
    {
    }

    String::String(const std::string_view str)
    {
        m_size = str.size();
        m_capacity = m_size + 1;
        m_data = allocateMemory(m_capacity);
        std::memcpy(m_data, str.data(), m_size);
        m_data[m_size] = '\0';
    }

    String::String(String&& other) noexcept: m_data(other.m_data), m_size(other.m_size),
                                             m_capacity(other.m_capacity)
    {
        other.m_data = nullptr;
        other.m_size = other.m_capacity = 0;
    }


    void String::reserve(const size_t capacity)
    {
        if (capacity <= m_capacity) return;

        char* newData = allocateMemory(capacity);
        if (m_data)
        {
            std::memcpy(newData, m_data, m_size + 1);
            deallocateMemory(m_data, m_capacity);
        }
        else
        {
            newData[0] = '\0';
        }
        m_data = newData;
        m_capacity = capacity;
    }


    String::~String()
    {
        deallocateMemory(m_data, m_capacity);
    }

    String& String::operator=(const String& other)
    {
        if (this != &other)
        {
            deallocateMemory(m_data, m_capacity);
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            if (other.m_data)
            {
                m_data = allocateMemory(m_capacity);
                std::memcpy(m_data, other.m_data, m_size + 1);
            }
            else
            {
                m_data = nullptr;
            }
        }
        return *this;
    }

    String& String::operator=(String&& other) noexcept
    {
        if (this != &other)
        {
            deallocateMemory(m_data, m_capacity);
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = nullptr;
            other.m_size = other.m_capacity = 0;
        }
        return *this;
    }

    String::operator std::string() const
    {
        return m_data ? std::string(m_data) : std::string();
    }

    String::operator std::string_view() const
    {
        return m_data ? std::string_view(m_data, m_size) : std::string_view();
    }

    [[nodiscard]] const char* String::c_str() const
    {
        return m_data ? m_data : "";
    }


    size_t String::length() const
    {
        return m_size;
    }

    size_t String::size() const
    {
        return m_size;
    }

    bool String::empty() const
    {
        return m_size == 0;
    }

    size_t String::capacity() const
    {
        return m_capacity;
    }

    bool String::operator==(const String& other) const
    {
        return m_size == other.m_size && std::memcmp(m_data, other.m_data, m_size) == 0;
    }

    bool String::operator==(const char* str) const
    {
        if (!str) return m_size == 0;
        return std::strcmp(m_data ? m_data : "", str) == 0;
    }

    bool String::operator!=(const String& other) const
    {
        return !(*this == other);
    }

    String& String::operator+=(const String& other)
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

    String& String::operator+=(const char* str)
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


    String String::operator+(const String& other) const
    {
        String result(*this);
        result += other;
        return result;
    }

    String String::operator+(const char* str) const
    {
        String result(*this);
        result += str;
        return result;
    }

    char& String::operator[](size_t index)
    {
        checkIndex(index);
        return m_data[index];
    }

    const char& String::operator[](size_t index) const
    {
        checkIndex(index);
        return m_data[index];
    }

    void String::clear()
    {
        deallocateMemory(m_data, m_capacity);
        m_data = nullptr;
        m_size = m_capacity = 0;
    }

    void String::checkIndex(size_t index) const
    {
        if (index >= m_size)
        {
            throw std::out_of_range("String index out of range");
        }
    }

    String operator+(const char* str, const String& other)
    {
        return String(str) + other;
    }
}
