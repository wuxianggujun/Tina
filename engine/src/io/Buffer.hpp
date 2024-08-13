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
    template <class T>
    class Buffer
      /**
       * 一个缓冲区类，用于在构造函数中分配给定类型和大小的缓冲区，
       * 并在析构函数中释放缓冲区。此类在需要临时缓冲区的任何地方都很有用。
       * 注意：缓冲区既有大小又有容量，类似于 std：：vector 和 std：：string。但是，在创建 Buffer 时，大小始终等于容量（通过构造函数的 length 参数提供），
       * 因为 Buffer 旨在通过直接写入其内容来填充，即将指针传递给通过 begin（） 获取的缓冲区的第一个元素的指针到期望指向缓冲区的指针的函数。
       * 因此，在新创建的 Buffer 上调用 append（） 将始终扩展缓冲区的大小和容量。
       * 如果需要创建一个 缓冲区 并希望通过调用 append（） 向其写入数据，正确的步骤是先创建 Buffer，然后调用 resize（0），然后调用 append（）。
       */
    {
    public:
        explicit Buffer(std::size_t length):
            _capacity(length),
            _used(length),
            _ptr(nullptr),
            _ownMem(true)
        /// Creates and allocates the Buffer.
        {
            if (length > 0)
            {
                _ptr = new T[length];
            }
        }

        Buffer(T* pMem, std::size_t length):
            _capacity(length),
            _used(length),
            _ptr(pMem),
            _ownMem(false)
        /// Creates the Buffer. Length argument specifies the length
        /// of the supplied memory pointed to by pMem in the number
        /// of elements of type T. Supplied pointer is considered
        /// blank and not owned by Buffer, so in this case Buffer
        /// only acts as a wrapper around externally supplied
        /// (and lifetime-managed) memory.
        {
        }

        Buffer(const T* pMem, std::size_t length):
            _capacity(length),
            _used(length),
            _ptr(nullptr),
            _ownMem(true)
        /// Creates and allocates the Buffer; copies the contents of
        /// the supplied memory into the buffer. Length argument specifies
        /// the length of the supplied memory pointed to by pMem in the
        /// number of elements of type T.
        {
            if (_capacity > 0)
            {
                _ptr = new T[_capacity];
                std::memcpy(_ptr, pMem, _used * sizeof(T));
            }
        }

        Buffer(const Buffer& other):
            /// Copy constructor.
            _capacity(other._used),
            _used(other._used),
            _ptr(nullptr),
            _ownMem(true)
        {
            if (_used)
            {
                _ptr = new T[_used];
                std::memcpy(_ptr, other._ptr, _used * sizeof(T));
            }
        }

        Buffer(Buffer&& other) noexcept:
            /// Move constructor.
            _capacity(other._capacity),
            _used(other._used),
            _ptr(other._ptr),
            _ownMem(other._ownMem)
        {
            other._capacity = 0;
            other._used = 0;
            other._ownMem = false;
            other._ptr = nullptr;
        }

        Buffer& operator =(const Buffer& other)
        /// Assignment operator.
        {
            if (this != &other)
            {
                Buffer tmp(other);
                swap(tmp);
            }

            return *this;
        }
        

        Buffer& operator =(Buffer&& other) noexcept
        /// Move assignment operator.
        {
            if (_ownMem) delete [] _ptr;

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

        ~Buffer()
        /// Destroys the Buffer.
        {
            if (_ownMem) delete [] _ptr;
        }

        void resize(std::size_t newCapacity, bool preserveContent = true)
        /**
         *调整缓冲区容量和大小。如果 preserveContent 为 true，则旧缓冲区的内容将复制到新缓冲区。
         *新容量可以大于或小于当前容量;如果它较小，容量将保持不变。
         *大小将始终设置为新容量。仅包装外部拥有的存储的缓冲区无法调整大小。
         *如果尝试对这些调整大小时，则会引发 IllegalAccessException
         */
        {
            if (!_ownMem) throw std::runtime_error("Cannot resize buffer which does not own its storage.");

            if (newCapacity > _capacity)
            {
                T* ptr = new T[newCapacity];
                if (preserveContent && _ptr)
                {
                    std::memcpy(ptr, _ptr, _used * sizeof(T));
                }
                delete [] _ptr;
                _ptr = ptr;
                _capacity = newCapacity;
            }

            _used = newCapacity;
        }

        void setCapacity(std::size_t newCapacity, bool preserveContent = true)
        /// Sets the buffer capacity. If preserveContent is true,
        /// the content of the old buffer is copied over to the
        /// new buffer. The new capacity can be larger or smaller than
        /// the current one; size will be set to the new capacity only if
        /// new capacity is smaller than the current size, otherwise it will
        /// remain intact.
        ///
        /// Buffers only wrapping externally owned storage can not be
        /// resized. If resize is attempted on those, IllegalAccessException
        /// is thrown.
        {
            if (!_ownMem) throw std::runtime_error("Cannot resize buffer which does not own its storage.");

            if (newCapacity != _capacity)
            {
                T* ptr = nullptr;
                if (newCapacity > 0)
                {
                    ptr = new T[newCapacity];
                    if (preserveContent && _ptr)
                    {
                        std::size_t newSz = _used < newCapacity ? _used : newCapacity;
                        std::memcpy(ptr, _ptr, newSz * sizeof(T));
                    }
                }
                delete [] _ptr;
                _ptr = ptr;
                _capacity = newCapacity;

                if (newCapacity < _used) _used = newCapacity;
            }
        }

    

        void assign(const T* buf, std::size_t sz)
        /// Assigns the argument buffer to this buffer.
        /// If necessary, resizes the buffer.
        {
            if (0 == sz) return;
            if (sz > _capacity) resize(sz, false);
            std::memcpy(_ptr, buf, sz * sizeof(T));
            _used = sz;
        }

        void append(const T* buf, std::size_t sz)
        /// Resizes this buffer and appends the argument buffer.
        {
            if (0 == sz) return;
            resize(_used + sz, true);
            std::memcpy(_ptr + _used - sz, buf, sz * sizeof(T));
        }

        void append(T val)
        /// Resizes this buffer by one element and appends the argument value.
        {
            resize(_used + 1, true);
            _ptr[_used - 1] = val;
        }

        void append(const Buffer& buf)
        /// Resizes this buffer and appends the argument buffer.
        {
            append(buf.begin(), buf.size());
        }

        [[nodiscard]] std::size_t capacity() const
        /// Returns the allocated memory size in elements.
        {
            return _capacity;
        }

        [[nodiscard]] std::size_t capacityBytes() const
        /// Returns the allocated memory size in bytes.
        {
            return _capacity * sizeof(T);
        }

        void swap(Buffer& other) noexcept
        /// Swaps the buffer with another one.
        {
            using std::swap;
            std::swap(_ptr, other._ptr);
            std::swap(_capacity, other._capacity);
            std::swap(_used, other._used);
            std::swap(_ownMem, other._ownMem);
        }

        bool operator ==(const Buffer& other) const
        /// Compare operator.
        {
            if (this != &other)
            {
                if (_used == other._used)
                {
                    if (_ptr && other._ptr && std::memcmp(_ptr, other._ptr, _used * sizeof(T)) == 0)
                    {
                        return true;
                    }
                    else return _used == 0;
                }
                return false;
            }

            return true;
        }

        bool operator !=(const Buffer& other) const
        /// Compare operator.
        {
            return !(*this == other);
        }

        void clear() const
        /// Sets the contents of the buffer to zero.
        {
            std::memset(_ptr, 0, _used * sizeof(T));
        }

        [[nodiscard]] std::size_t size() const
        /// Returns the used size of the buffer in elements.
        {
            return _used;
        }

        [[nodiscard]] std::size_t sizeBytes() const
        /// Returns the used size of the buffer in bytes.
        {
            return _used * sizeof(T);
        }

        T* begin()
        /// Returns a pointer to the beginning of the buffer.
        {
            return _ptr;
        }

        const T* begin() const
        /// Returns a pointer to the beginning of the buffer.
        {
            return _ptr;
        }

        T* end()
        /// Returns a pointer to end of the buffer.
        {
            return _ptr + _used;
        }

        const T* end() const
        /// Returns a pointer to the end of the buffer.
        {
            return _ptr + _used;
        }

        [[nodiscard]] bool empty() const
        /// Return true if buffer is empty.
        {
            return 0 == _used;
        }

        T& operator [](std::size_t index)
        {
            if(index < _used)
            {
                throw std::runtime_error("Index out of range");
            }
            return _ptr[index];
        }

        const T& operator [](std::size_t index) const
        {
            if (index >= _used) {
                throw std::out_of_range("Index out of bounds");
            }
            return _ptr[index];
        }

    private:
        Buffer();

        std::size_t _capacity;
        std::size_t _used;
        T* _ptr;
        bool _ownMem;
    };
}


#endif //TINA_FILESYSTEM_BUFFER_HPP
