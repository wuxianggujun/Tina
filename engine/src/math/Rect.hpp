//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once
#include "Vector.hpp"

namespace Tina
{
    template <class T>
    class Rect
    {
    public:
        Rect(): left(0), top(0), width(0), height(0)
        {
        };

        explicit Rect(T left, T top, T width, T height): left(left), top(top), width(width), height(height)
        {
        };

        // 使用 typename 关键字来明确这是一个类型
        Rect(const typename Tina::Vector<T>& position, const typename Tina::Vector<T>& size)
            : left(position.x), top(position.y), width(size.x), height(size.y) {}

        Vector<T> getPosition() const { return Vector<T>(left, top); }
        Vector<T> getSize() const { return Vector<T>(width, height); }
        T getRight() const { return left + width; }
        T getBottom() const { return top + height; }
        Vector<T> getCenter() const { return Vector<T>(left + width / 2, top + height / 2); }

        void setPosition(const Vector<T>& position)
        {
            left = position.x;
            top = position.y;
        }

        void setSize(const Vector<T>& size)
        {
            width = size.x;
            height = size.y;
        }

        bool intersects(const Rect<T>& other) const
        {
            return !(left >= other.getRight() || getRight() <= other.left || top >= other.getBottom() || getBottom() <=
                other.top);
        }

        bool contains(const Vector<T>& point) const
        {
            return point.x >= left &&
                point.x < left + width &&
                point.y >= top &&
                point.y < top + height;
        }

        // 运算符重载
        bool operator==(const Rect<T>& other) const
        {
            return left == other.left && top == other.top &&
                width == other.width && height == other.height;
        }

        bool operator!=(const Rect<T>& other) const
        {
            return !(*this == other);
        }

        T left, top;
        T width, height;
    };

    // 常用类型定义
    using Rectf = Rect<float>;
    using Recti = Rect<int>;
    using Rectd = Rect<double>;
}
