//
// Created by wuxianggujun on 2025/2/16.
//
#pragma once

#include <glm/vec2.hpp>
#include "tina/core/Core.hpp"

namespace Tina
{
    class TINA_CORE_API Rect
    {
    public:
        Rect() = default;

        explicit Rect(const float x, const float y, const float width, const float height): x(x), y(y), width(width),
            height(height)
        {
        }

        float getLeft() const { return x; }
        float getRight() const { return x + width; }
        float getTop() const { return y; }
        float getBottom() const { return y + height; }

        [[nodiscard]] float getX() const
        {
            return x;
        }

        void setX(float x)
        {
            this->x = x;
        }

        [[nodiscard]] float getY() const
        {
            return y;
        }

        void setY(float y)
        {
            this->y = y;
        }

        [[nodiscard]] float getWidth() const
        {
            return width;
        }

        void setWidth(float width)
        {
            this->width = width;
        }

        [[nodiscard]] float getHeight() const
        {
            return height;
        }

        void setHegiht(float height)
        {
            this->height = height;
        }

        //获取中心点
        glm::vec2 getCenter() const
        {
            return {x + width * 0.5f, y + height * 0.5f};
        }

        // 获取尺寸
        glm::vec2 getSize() const
        {
            return {width, height};
        }

        // 设置位置
        void setPosition(const glm::vec2& pos)
        {
            x = pos.x;
            y = pos.y;
        }

        // 设置尺寸
        void setSize(const glm::vec2& size)
        {
            width = size.x;
            height = size.y;
        }

        // 点是否在矩形内
        bool contains(const glm::vec2& point) const
        {
            return point.x >= x && point.x <= getRight() &&
                point.y >= y && point.y <= getBottom();
        }

        // 是否与另一个矩形相交
        bool intersects(const Rect& other) const
        {
            return !(other.x > getRight() ||
                other.getRight() < x ||
                other.y > getBottom() ||
                other.getBottom() < y);
        }

        // 获取两个矩形的交集
        Rect getIntersection(const Rect& other) const
        {
            float left = std::max(x, other.x);
            float right = std::min(getRight(), other.getRight());
            float top = std::max(y, other.y);
            float bottom = std::min(getBottom(), other.getBottom());

            if (left < right && top < bottom)
            {
                return Rect(left, top, right - left, bottom - top);
            }
            return Rect();
        }

        // 合并两个矩形
        Rect getUnion(const Rect& other) const
        {
            float left = std::min(x, other.x);
            float right = std::max(getRight(), other.getRight());
            float top = std::min(y, other.y);
            float bottom = std::max(getBottom(), other.getBottom());

            return Rect(left, top, right - left, bottom - top);
        }

    private:
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };
} // Tina
