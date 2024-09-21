//
// Created by wuxianggujun on 2024/7/20.
//

#ifndef TINA_MEMORY_ALLOCATOR_HPP
#define TINA_MEMORY_ALLOCATOR_HPP

#include <new>
#include <cstddef>

namespace Tina
{
    class Allocator
    {
    protected:
        ~Allocator() = default;

    public:
        inline virtual void* allocate(size_t arg_size, size_t arg_alignment) = 0;
        inline virtual void deallocate(void* arg_ptr) = 0;
    };

    class MallocAllocator : public Allocator
    {
    protected:
        MallocAllocator() = default;
        ~MallocAllocator() = default;

    public:
        void* allocate(size_t arg_size, size_t arg_alignment) override;
        void deallocate(void* arg_ptr) override;
    };
} // Tina

#endif //TINA_MEMORY_ALLOCATOR_HPP
