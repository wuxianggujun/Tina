//
// Created by wuxianggujun on 2024/7/31.
// https://github.com/pocoproject/poco/blob/1eebd46c0487356fc3c89991ddc160ab97a6f001/Foundation/include/Poco/Buffer.h
// 

#ifndef TINA_FILESYSTEM_BUFFER_HPP
#define TINA_FILESYSTEM_BUFFER_HPP

#include <cstring>
#include <cstddef>
#include <memory>

namespace Tina
{
    /*
     * 一个非常简单的缓冲区类，它在构造函数中给指定类型分配一个缓冲区
     * 并在析构函数中释放该缓冲区
     * 这个类咋需要一个临时缓冲区的地方非常有用
     *
     */

    template <class T>
    class Buffer
    {
    public:
        explicit Buffer(size_t length): _capacity(length), _used(length), _ptr(nullptr), _ownMem(true)
        {
            if (length > 0)
            {
                _ptr = new T[length];
            }
        }

        Buffer(T* pMem, size_t length): _capacity(length), _used(length), _ptr(pMem), _ownMem(false)
        {
        }

        Buffer(const T* pMem, size_t length): _capacity(length), _used(length), _ptr(nullptr), _ownMem(true)
        {
            if (_capacity > 0)
            {
                _ptr = new T[_capacity];
                memccpy(_ptr, pMem, _used * sizeof(T));
            }
        }

        Buffer(const Buffer& other): _capacity(other._used), _used(other._used), _ptr(nullptr), _ownMem(true)
        {
            if (_used)
            {
                _ptr = new T[_used];
                memcpy(_ptr, other._ptr, _used * sizeof(T));
            }
        }

        Buffer(Buffer& other) noexcept : _capacity(other._capacity), _used(other._used), _ptr(other._ptr),
                                         _ownMem(other._ownMem)
        {
            other._capacity = 0;
            other._used = 0;
            other._ownMem = false;
            other._ptr = nullptr;
        }

        Buffer& operator=(const Buffer& other)
        {
            if (this != &other)
            {
                Buffer tmp(other);
                swap(tmp);
            }
            return *this;
        }


        Buffer& operator=(Buffer&& other) noexcept
        {
            if (_ownMem) delete[] _ptr;
            _capacity = other._capacity;
            _used = other._used;
            _ptr = other._ptr;
            _ownMem = other._ownMem;


            other._capacity = 0;
            other._used = 0;
            other._ownMem = false;
            other._ptr = nullptr;
            return *this;
        }


        ~Buffer() //销毁Buffer/**/
        {
            if (_ownMem)
                delete[] _ptr; //释放分配的内存
        }

        // 调整缓冲区的大小
        // @param newSize 新的缓冲区大小
        // @param preserveContent 是否保留缓冲区内容
        // @note 如果 preserveContent 为 true，则缓冲区内容将保留在调整后的缓冲区中

        void resize(size_t newCapacity, bool preserveContent = true)
        {
            if (!_ownMem) return;

            if (newCapacity > _capacity)
            {
                T* ptr = new T[newCapacity];
                if (preserveContent && _ptr)
                {
                    memcpy(ptr, _ptr, _used * sizeof(T));
                }
                // 释放旧缓冲区
                delete[] _ptr;
                // 更新指针
                _ptr = ptr;
                // 更新大小
                _capacity = newCapacity;
            }
            _used = newCapacity;
        }


        void setCapacity(size_t newCapacity, bool preserveContent = true)
        {
            if (!_ownMem) return;

            if (newCapacity != _capacity)
            {
                T* ptr = nullptr;
                if (newCapacity > 0)
                {
                    ptr = new T[newCapacity];
                    if (preserveContent && _ptr)
                    {
                        size_t newSize = _used < newCapacity ? _used : newCapacity;
                        memcpy(ptr, _ptr, newSize * sizeof(T));
                    }
                }

                delete[] _ptr;
                _ptr = ptr;
                _capacity = newCapacity;
                if (newCapacity < _used) _used = newCapacity;
            }
        }

        void assign(const T* buf, size_t sz)
        {
            if (0 == sz) return;
            if (sz > _capacity) resize(sz, false);
            memccpy(_ptr, buf, sz * sizeof(T));
            _used = sz;
        }

        void append(const T* buf, size_t sz)
        {
            if (0 == sz) return;
            resize(_used + sz, true);
            memccpy(_ptr + _used - sz, buf, sz * sizeof(T));
        }

        void append(T val)
        {
            resize(_used + 1, true);
            _ptr[_used - 1] = val;
        }

        void append(const Buffer& buf)
        {
            append(buf.begin(), buf.size());
        }

        [[nodiscard]] size_t capacity() const
        {
            return _capacity;
        }

        [[nodiscard]] size_t capacityBytes() const
        {
            return _capacity * sizeof(T);
        }

        void swap(Buffer& other) noexcept
        {
            using std::swap;
            swap(_ptr, other._ptr);
            swap(_capacity, other._capacity);
            swap(_used, other._used);
            swap(_ownMem, other._ownMem);
        }

        bool operator==(const Buffer& other) const
        {
            if (this != &other)
            {
                if (_used == other._used)
                {
                    if (_ptr && other._ptr && memcmp(_ptr, other._ptr, _used * sizeof(T)) == 0)
                    {
                        return true;
                    }
                    return _used == 0;
                }
                return false;
            }
            return *this;
        }

        bool operator!=(const Buffer& other) const
        {
            return !(*this == other);
        }

        void clear()
        {
            memset(_ptr, 0, _used * sizeof(T));
        }

        [[nodiscard]] size_t size() const
        {
            return _used;
        }

        [[nodiscard]] size_t sizeBytes() const
        {
            return _used * sizeof(T);
        }

        // 返回指向缓冲区开始的指针
        T* begin()
        {
            return _ptr;
        }

        const T* begin() const
        {
            return _ptr;
        }

        // 返回指向缓冲区结束的指针
        T* end()
        {
            return _ptr + _used;
        }

        const T* end() const
        {
            return _ptr + _used;
        }

        [[nodiscard]] bool empty() const
        {
            return 0 == _used;
        }

        // 重载下标操作符，允许通过下标访问元素
        T& operator[](size_t index)
        {
            return _ptr[index];
        }

        const T& operator[](size_t index) const
        {
            return _ptr[index];
        }

    private:
        Buffer() = default; // 私有默认构造函数

        size_t _capacity;
        size_t _used;
        T* _ptr; // 指向分配的内存的指针
        bool _ownMem;
    };
}


#endif //TINA_FILESYSTEM_BUFFER_HPP
