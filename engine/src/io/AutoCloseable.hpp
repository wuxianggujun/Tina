//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_AUTOCLOSEABLE_HPP
#define TINA_FILESYSTEM_AUTOCLOSEABLE_HPP

namespace Tina
{
    class AutoCloseable
    {
    public:
        virtual void close() = 0;
        virtual ~AutoCloseable() = default;
    };
}

#endif //TINA_FILESYSTEM_AUTOCLOSEABLE_HPP
