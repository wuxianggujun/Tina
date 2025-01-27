//
// Created by wuxianggujun on 2025/1/26.
//

#include "String.hpp"

#include <fmt/base.h>

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

    void StringMemoryPool::updateAllocationStats(size_t size)
    {
        m_stats.totalAllocations++;
        m_stats.currentAllocations++;
        m_stats.peakAllocations = std::max(m_stats.peakAllocations, m_stats.currentAllocations);

        m_stats.totalMemoryUsed += size;
        m_stats.currentMemoryUsed += size;
        m_stats.peakMemoryUsed = std::max(m_stats.peakMemoryUsed, m_stats.currentMemoryUsed);

        if (size <= SMALL_STRING_SIZE)
        {
            updatePoolStats(m_stats.smallPool, true);
        }
        else if (size <= MEDIUM_STRING_SIZE)
        {
            updatePoolStats(m_stats.mediumPool, true);
        }
        else if (size <= LARGE_STRING_SIZE)
        {
            updatePoolStats(m_stats.largePool, true);
        }
        else
        {
            updatePoolStats(m_stats.customAllocations, true);
        }
    }

    void StringMemoryPool::updateDeallocationStats(size_t size)
    {
        m_stats.currentAllocations--;
        m_stats.currentMemoryUsed -= size;

        if (size <= SMALL_STRING_SIZE)
        {
            updatePoolStats(m_stats.smallPool, false);
        }
        else if (size <= MEDIUM_STRING_SIZE)
        {
            updatePoolStats(m_stats.mediumPool, false);
        }
        else if (size <= LARGE_STRING_SIZE)
        {
            updatePoolStats(m_stats.largePool, false);
        }
        else
        {
            updatePoolStats(m_stats.customAllocations, false);
        }
    }

    void StringMemoryPool::updatePoolStats(PoolStats::PoolTypeStats& stats, bool isAllocation)
    {
        if (isAllocation)
        {
            stats.allocations++;
            stats.currentUsage++;
            stats.peakUsage = std::max(stats.peakUsage, stats.currentUsage);
        }
        else
        {
            stats.deallocations++;
            stats.currentUsage--;
        }
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
        char* ptr = nullptr;

        if (size <= SMALL_STRING_SIZE)
        {
            ptr = allocateFromPool(m_blocks, size);
        }
        else if (size <= MEDIUM_STRING_SIZE)
        {
            ptr = allocateFromPool(m_mediumPool, size);
        }
        else if (size <= LARGE_STRING_SIZE)
        {
            ptr = allocateFromPool(m_largePool, size);
        }
        else
        {
            ptr = new char[size];
        }

        if (ptr)
        {
            updateAllocationStats(size);
        }
        return ptr;
    }

    void StringMemoryPool::deallocate(char* ptr, size_t size)
    {
        if (!ptr) return;

        updateDeallocationStats(size);

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

    const StringMemoryPool::PoolStats& StringMemoryPool::getStats() const
    {
        return m_stats;
    }

    void StringMemoryPool::resetStats()
    {
        m_stats = PoolStats{};
    }

    void StringMemoryPool::printStats() const
    {
        fmt::print("String Memory Pool Statistics:\n");
        fmt::print("Total allocations: {}\n", m_stats.totalAllocations);
        fmt::print("Current allocations: {}\n", m_stats.currentAllocations);
        fmt::print("Peak allocations: {}\n", m_stats.peakAllocations);
        fmt::print("Total memory used: {} bytes\n", m_stats.totalMemoryUsed);
        fmt::print("Current memory used: {} bytes\n", m_stats.currentMemoryUsed);
        fmt::print("Peak memory used: {} bytes\n", m_stats.peakMemoryUsed);

        fmt::print("\nPool-specific statistics:\n");

        fmt::print("Small Pool:\n  {}\n", m_stats.smallPool);
        fmt::print("Medium Pool:\n  {}\n", m_stats.mediumPool);
        fmt::print("Large Pool:\n  {}\n", m_stats.largePool);
        fmt::print("Custom Allocations:\n  {}\n", m_stats.customAllocations); // 这里之前用错了变量
    }


    String::String() : m_size(0), m_isSSO(true)
    {
        sso.data[0] = '\0';
    }

    String::String(const String& other) : m_size(other.m_size), m_isSSO(other.m_isSSO)
    {
        copyFrom(other);
    }

    String::String(const char* str)
    {
        assert(str != nullptr && "Input String cannot be null");
        m_size = std::strlen(str);
        m_isSSO = canUseSSO(m_size);

        if (m_isSSO)
        {
            std::memcpy(sso.data, str, m_size + 1);
        }
        else
        {
            heap.m_capacity = m_size + 1;
            heap.m_data = allocateMemory(heap.m_capacity);
            std::memcpy(heap.m_data, str, m_size + 1);
        }
    }

    String::String(const std::string& str) : String(str.c_str())
    {
    }

    String::String(std::string_view str)
    {
        m_size = str.size();
        m_isSSO = canUseSSO(m_size);

        if (m_isSSO)
        {
            std::memcpy(sso.data, str.data(), m_size);
            sso.data[m_size] = '\0';
        }
        else
        {
            heap.m_capacity = m_size + 1;
            heap.m_data = allocateMemory(heap.m_capacity);
            std::memcpy(heap.m_data, str.data(), m_size);
            heap.m_data[m_size] = '\0';
        }
    }


    String::String(String&& other) noexcept : m_size(other.m_size), m_isSSO(other.m_isSSO)
    {
        moveFrom(std::move(other));
    }

    String::~String()
    {
        if (!m_isSSO && heap.m_data)
        {
            deallocateMemory(heap.m_data, heap.m_capacity);
        }
    }

    void String::moveFrom(String&& other) noexcept
    {
        if (m_isSSO)
        {
            std::memcpy(sso.data, other.sso.data, m_size + 1);
        }
        else
        {
            heap.m_data = other.heap.m_data;
            heap.m_capacity = other.heap.m_capacity;
            other.heap.m_data = nullptr;
        }
        other.m_size = 0;
        other.m_isSSO = true;
        other.sso.data[0] = '\0';
    }

    void String::copyFrom(const String& other)
    {
        if (m_isSSO)
        {
            std::memcpy(sso.data, other.sso.data, m_size + 1);
        }
        else
        {
            heap.m_capacity = other.heap.m_capacity;
            heap.m_data = allocateMemory(heap.m_capacity);
            std::memcpy(heap.m_data, other.heap.m_data, m_size + 1);
        }
    }

    String& String::operator=(const String& other)
    {
        if (this != &other)
        {
            if (!m_isSSO && heap.m_data)
            {
                deallocateMemory(heap.m_data, heap.m_capacity);
            }
            m_size = other.m_size;
            m_isSSO = other.m_isSSO;
            copyFrom(other);
        }
        return *this;
    }

    String& String::operator=(String&& other) noexcept
    {
        if (this != &other)
        {
            if (!m_isSSO && heap.m_data)
            {
                deallocateMemory(heap.m_data, heap.m_capacity);
            }
            m_size = other.m_size;
            m_isSSO = other.m_isSSO;
            moveFrom(std::move(other));
        }
        return *this;
    }

    void String::reserve(size_t newCapacity)
    {
        // 如果新容量小于等于当前容量，直接返回
        if (newCapacity <= getCapacity()) return;

        // 如果当前是SSO模式
        if (m_isSSO)
        {
            // 只有当新容量大于SSO容量时才切换到堆
            if (newCapacity > SSO_CAPACITY)
            {
                switchToHeap(newCapacity);
            }
        }
        else
        {
            // 已经在堆上，分配新的更大空间
            char* newData = allocateMemory(newCapacity);
            std::memcpy(newData, heap.m_data, m_size + 1);
            deallocateMemory(heap.m_data, heap.m_capacity);
            heap.m_data = newData;
            heap.m_capacity = newCapacity;
        }
    }

    void String::switchToHeap(size_t newCapacity)
    {
        char* newData = allocateMemory(newCapacity);
        std::memcpy(newData, sso.data, m_size + 1);
        heap.m_data = newData;
        heap.m_capacity = newCapacity;
        m_isSSO = false;
    }

    void String::tryToSwitchToSSO()
    {
        if (!m_isSSO && m_size <= SSO_CAPACITY)
        {
            char* oldData = heap.m_data;
            size_t oldCapacity = heap.m_capacity;
            std::memcpy(sso.data, oldData, m_size + 1);
            deallocateMemory(oldData, oldCapacity);
            m_isSSO = true;
        }
    }

    void String::shrink_to_fit()
    {
        if (m_isSSO) return;

        if (m_size <= SSO_CAPACITY)
        {
            tryToSwitchToSSO();
        }
        else if (heap.m_capacity > m_size + 1)
        {
            char* newData = allocateMemory(m_size + 1);
            std::memcpy(newData, heap.m_data, m_size + 1);
            deallocateMemory(heap.m_data, heap.m_capacity);
            heap.m_data = newData;
            heap.m_capacity = m_size + 1;
        }
    }

    void String::resize(size_t newSize, char ch)
    {
        if (newSize > getCapacity())
        {
            reserve(std::max(newSize, getCapacity() * 2));
        }

        char* data = getDataPtr();
        if (newSize > m_size)
        {
            std::fill(data + m_size, data + newSize, ch);
        }
        data[newSize] = '\0';
        m_size = newSize;
        tryToSwitchToSSO();
    }

    void String::clear()
    {
        if (m_isSSO)
        {
            sso.data[0] = '\0';
        }
        else
        {
            heap.m_data[0] = '\0';
        }
        m_size = 0;
        tryToSwitchToSSO();
    }

    String& String::operator+=(const String& other)
    {
        if (!other.empty())
        {
            size_t newSize = m_size + other.m_size;
            if (newSize + 1 > getCapacity())
            {
                // 如果当前是SSO模式且新大小超过SSO容量，强制切换到堆
                if (m_isSSO)
                {
                    size_t newCapacity = std::max(newSize + 1, SSO_CAPACITY * 2);
                    switchToHeap(newCapacity);
                }
                else
                {
                    // 已经在堆上，继续扩容
                    size_t newCapacity = std::max(newSize + 1, heap.m_capacity * 2);
                    char* newData = allocateMemory(newCapacity);
                    std::memcpy(newData, heap.m_data, m_size + 1);
                    deallocateMemory(heap.m_data, heap.m_capacity);
                    heap.m_data = newData;
                    heap.m_capacity = newCapacity;
                }
            }
            std::memcpy(getDataPtr() + m_size, other.getDataPtr(), other.m_size + 1);
            m_size = newSize;
        }
        return *this;
    }

    String& String::operator+=(const char* str)
    {
        if (str)
        {
            size_t len = std::strlen(str);
            if (len > 0)
            {
                size_t newSize = m_size + len;
                if (newSize + 1 > getCapacity())
                {
                    reserve(std::max(newSize + 1, getCapacity() * 2));
                }
                std::memcpy(getDataPtr() + m_size, str, len + 1);
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

    // 类型转换操作符
    String::operator std::string() const
    {
        return getDataPtr() ? std::string(getDataPtr()) : std::string();
    }

    String::operator std::string_view() const
    {
        return getDataPtr() ? std::string_view(getDataPtr(), m_size) : std::string_view();
    }

    // 访问方法
    const char* String::c_str() const
    {
        return getDataPtr();
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
        if (m_isSSO)
        {
            return m_size == 0 ? 0 : SSO_CAPACITY; // 空字符串返回0，否则返回SSO容量
        }
        return heap.m_capacity;
    }

    // 比较操作符
    bool String::operator==(const String& other) const
    {
        if (m_size != other.m_size) return false;
        return std::memcmp(getDataPtr(), other.getDataPtr(), m_size) == 0;
    }

    bool String::operator==(const char* str) const
    {
        if (!str) return m_size == 0;
        return std::strcmp(getDataPtr(), str) == 0;
    }

    bool String::operator!=(const String& other) const
    {
        return !(*this == other);
    }

    // 索引访问
    char& String::operator[](size_t index)
    {
        checkIndex(index);
        return getDataPtr()[index];
    }

    const char& String::operator[](size_t index) const
    {
        checkIndex(index);
        return getDataPtr()[index];
    }

    // 查找方法
    size_t String::find(const String& str, size_t pos) const
    {
        if (pos > m_size) return npos;
        const char* found = std::strstr(getDataPtr() + pos, str.getDataPtr());
        return found ? found - getDataPtr() : npos;
    }

    size_t String::find(const char* str, size_t pos) const
    {
        if (!str || pos > m_size) return npos;
        const char* found = std::strstr(getDataPtr() + pos, str);
        return found ? found - getDataPtr() : npos;
    }

    size_t String::rfind(const String& str, size_t pos) const
    {
        if (str.empty()) return pos > m_size ? m_size : pos;
        if (str.size() > m_size) return npos;

        pos = std::min(pos, m_size - str.size());
        for (size_t i = pos + 1; i-- > 0;)
        {
            if (std::memcmp(getDataPtr() + i, str.getDataPtr(), str.size()) == 0)
            {
                return i;
            }
        }
        return npos;
    }

    size_t String::rfind(const char* str, size_t pos) const
    {
        return rfind(String(str), pos);
    }

    // 子串操作
    String String::substr(size_t pos, size_t len) const
    {
        if (pos > m_size) throw std::out_of_range("String::substr");
        len = std::min(len, m_size - pos);
        return String(std::string_view(getDataPtr() + pos, len));
    }

    void String::append(const char* str, size_t n)
    {
        if (!str || n == 0) return;
        size_t newSize = m_size + n;
        if (newSize + 1 > getCapacity())
        {
            reserve(std::max(newSize + 1, getCapacity() * 2));
        }
        std::memcpy(getDataPtr() + m_size, str, n);
        m_size = newSize;
        getDataPtr()[m_size] = '\0';
    }

    void String::append(const String& str)
    {
        append(str.getDataPtr(), str.size());
    }

    void String::insert(size_t pos, const String& str)
    {
        if (pos > m_size) throw std::out_of_range("String::insert");
        size_t newSize = m_size + str.size();
        if (newSize + 1 > getCapacity())
        {
            reserve(std::max(newSize + 1, getCapacity() * 2));
        }
        std::memmove(getDataPtr() + pos + str.size(), getDataPtr() + pos, m_size - pos + 1);
        std::memcpy(getDataPtr() + pos, str.getDataPtr(), str.size());
        m_size = newSize;
    }

    void String::insert(size_t pos, const char* str)
    {
        insert(pos, String(str));
    }

    void String::erase(size_t pos, size_t len)
    {
        if (pos > m_size) throw std::out_of_range("String::erase");
        if (len == npos || pos + len > m_size)
        {
            len = m_size - pos;
        }
        if (len > 0)
        {
            std::memmove(getDataPtr() + pos, getDataPtr() + pos + len, m_size - pos - len + 1);
            m_size -= len;
            tryToSwitchToSSO();
        }
    }

    void String::replace(size_t pos, size_t len, const String& str)
    {
        if (pos > m_size) throw std::out_of_range("String::replace");
        if (len == npos || pos + len > m_size)
        {
            len = m_size - pos;
        }

        size_t newSize = m_size - len + str.size();
        if (newSize + 1 > getCapacity())
        {
            reserve(std::max(newSize + 1, getCapacity() * 2));
        }

        std::memmove(getDataPtr() + pos + str.size(),
                     getDataPtr() + pos + len,
                     m_size - pos - len + 1);
        std::memcpy(getDataPtr() + pos, str.getDataPtr(), str.size());
        m_size = newSize;
        tryToSwitchToSSO();
    }

    void String::replace(size_t pos, size_t len, const char* str)
    {
        replace(pos, len, String(str));
    }
}
