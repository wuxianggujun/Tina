//
// Created by wuxianggujun on 2024/7/14.
//

#ifndef TINA_MEMORY_CHECKED_DELETE_HPP
#define TINA_MEMORY_CHECKED_DELETE_HPP

namespace Tina
{
    template <class T>
    inline void checked_delete(T*& p)
    {
        static_assert(sizeof(T), "Trying to delete a pointer to an incomplete type,");
        delete p;
        p = nullptr;
    }

    template <class T>
    inline void checked_array_delete(T*& p)
    {
        static_assert(sizeof(T), "Trying to delete a pointer to an incomplete type,");
        delete[] p;
        p = nullptr;
    }

    template <class T>
    struct checked_deleter
    {
        void operator()(T*& p) const
        {
            checked_delete(p);
        }
    };

    template <class T>
    struct checked_array_deleter
    {
        void operator()(T*& p) const
        {
            checked_array_delete(p);
        }
    };
}


#endif //TINA_MEMORY_CHECKED_DELETE_HPP
