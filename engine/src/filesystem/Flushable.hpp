//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_FLUSHABLE_HPP
#define TINA_FILESYSTEM_FLUSHABLE_HPP

namespace Tina
{
    class Flushable
    {
    public:
        virtual ~Flushable() = default;
        virtual void flush() = 0;
    };
};

#endif //TINA_FILESYSTEM_FLUSHABLE_HPP
