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

        explicit String(const std::string& str): String(str.c_str())
        {
        }

        explicit String(const std::string_view str)
        {
            m_size = str.size();
            m_capacity = m_size + 1;
            m_data = new char[m_capacity];
            std::memcpy(m_data, str.data(), m_size);
            m_data[m_size] = '\0';
        }

        String(String&& other) noexcept: m_data(other.m_data), m_size(other.m_size),
                                         m_capacity(other.m_capacity)
        {
            other.m_data = nullptr;
            other.m_size = other.m_capacity = 0;
        }

        ~String()
        {
            delete[] m_data;
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

        explicit operator std::string() const
        {
            return m_data ? std::string(m_data) : std::string();
        }

        explicit operator std::string_view() const
        {
            return m_data ? std::string_view(m_data, m_size) : std::string_view();
        }

        [[nodiscard]] const char* c_str() const
        {
            return m_data ? m_data : "";
        }

        [[nodiscard]] size_t length() const
        {
            return m_size;
        }

        [[nodiscard]] size_t size() const
        {
            return m_size;
        }

        [[nodiscard]] bool empty() const
        {
            return m_size == 0;
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
