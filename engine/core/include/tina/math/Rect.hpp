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

        /**
         * @brief 构造函数
         * @param x 左上角x坐标
         * @param y 左上角y坐标
         * @param width 宽度
         * @param height 高度
        */
        explicit Rect(const float x, const float y, const float width, const float height): x(x), y(y), width(width),
            height(height)
        {
        }

        /**
         * @brief 构造函数
         * @param position 左上角位置
         * @param size 大小
         */
        explicit Rect(const glm::vec2& position, const glm::vec2& size)
            : x(position.x), y(position.y), width(size.x), height(size.y)
        {
        }

        /**
         * @brief 获取矩形位置
         * @return 矩形位置
         */
        [[nodiscard]] glm::vec2 getPosition() const { return {x, y}; }

        /**
         * @brief 设置矩形位置
         * @param position 矩形位置
         */
        void setPosition(const glm::vec2& position)
        {
            x = position.x;
            y = position.y;
        }


        [[nodiscard]] float getLeft() const { return x; }
        [[nodiscard]] float getRight() const { return x + width; }
        [[nodiscard]] float getTop() const { return y; }
        [[nodiscard]] float getBottom() const { return y + height; }

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

        void setHeight(float height)
        {
            this->height = height;
        }

        /**
         * @brief 获取矩形大小
         * @return 矩形大小
         */
        [[nodiscard]] glm::vec2 getSize() const { return {width, height}; }

        /**
         * @brief 设置矩形大小
         * @param size 矩形大小
         */
        void setSize(const glm::vec2& size)
        {
            width = size.x;
            height = size.y;
        }

        /**
         * @brief 获取矩形中心点
         * @return 矩形中心点
         */
        [[nodiscard]] glm::vec2 getCenter() const
        {
            return {x + width * 0.5f, y + height * 0.5f};
        }

        /**
         * @brief 获取矩形右下角点
         * @return 矩形右下角点
         */
        [[nodiscard]] glm::vec2 getBottomRight() const
        {
            return {x + width, y + height};
        }

        /**
         * @brief 检查点是否在矩形内
         * @param point 要检查的点
         * @return 是否在矩形内
         */
        [[nodiscard]] bool contains(const glm::vec2& point) const
        {
            return point.x >= x && point.x <= x + width &&
                point.y >= y && point.y <= y + height;
        }

        /**
         * @brief 检查矩形是否与另一个矩形相交
         * @param other 另一个矩形
         * @return 是否相交
         */
        [[nodiscard]] bool intersects(const Rect& other) const
        {
            return x < other.x + other.width && x + width > other.x &&
                y < other.y + other.height && y + height > other.y;
        }

        /**
         * @brief 获取与另一个矩形的相交区域
         * @param other 另一个矩形
         * @return 相交区域
         */
        [[nodiscard]] Rect getIntersection(const Rect& other) const
        {
            float left = std::max(x, other.x);
            float top = std::max(y, other.y);
            float right = std::min(x + width, other.x + other.width);
            float bottom = std::min(y + height, other.y + other.height);

            if (left < right && top < bottom)
            {
                return Rect(left, top, right - left, bottom - top);
            }

            return {};
        }

        /**
     * @brief 获取包含另一个矩形的最小矩形
     * @param other 另一个矩形
     * @return 包含两个矩形的最小矩形
     */
        [[nodiscard]] Rect getUnion(const Rect& other) const
        {
            float left = std::min(x, other.x);
            float top = std::min(y, other.y);
            float right = std::max(x + width, other.x + other.width);
            float bottom = std::max(y + height, other.y + other.height);

            return Rect(left, top, right - left, bottom - top);
        }

        /**
       * @brief 扩展矩形
       * @param amount 扩展量
       */
        void inflate(float amount)
        {
            x -= amount;
            y -= amount;
            width += amount * 2;
            height += amount * 2;
        }

        /**
         * @brief 缩小矩形
         * @param amount 缩小量
         */
        void deflate(float amount)
        {
            inflate(-amount);
        }

        /**
         * @brief 移动矩形
         * @param offset 移动偏移量
         */
        void move(const glm::vec2& offset)
        {
            x += offset.x;
            y += offset.y;
        }

        /**
         * @brief 缩放矩形
         * @param scale 缩放因子
         */
        void scale(const glm::vec2& scale)
        {
            width *= scale.x;
            height *= scale.y;
        }

        /**
         * @brief 规范化矩形
         *
         * 确保宽度和高度为正数，如果为负数则调整位置和大小
         */
        void normalize()
        {
            if (width < 0)
            {
                x += width;
                width = -width;
            }
            if (height < 0)
            {
                y += height;
                height = -height;
            }
        }

        /**
         * @brief 判断矩形是否为空
         * @return 是否为空
         */
        [[nodiscard]] bool isEmpty() const
        {
            return width <= 0 || height <= 0;
        }

        /**
         * @brief 判断矩形是否相等
         * @param other 另一个矩形
         * @return 是否相等
         */
        bool operator==(const Rect& other) const
        {
            return x == other.x && y == other.y &&
                width == other.width && height == other.height;
        }

        /**
         * @brief 判断矩形是否不相等
         * @param other 另一个矩形
         * @return 是否不相等
         */
        bool operator!=(const Rect& other) const
        {
            return !(*this == other);
        }

    private:
        float x = 0; ///< 矩形左上角x坐标
        float y = 0; ///< 矩形左上角y坐标
        float width = 0; ///< 矩形宽度
        float height = 0; ///< 矩形高度
    };
} // Tina
