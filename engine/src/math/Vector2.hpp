//
// Created by wuxianggujun on 2024/7/15.
// https://github.com/wobbier/MitchEngine/blob/9bf6fc65821c486eccc3d1d83fef4e6b68b6450b/Modules/Dementia/Source/Math/Vector2.h
//

#ifndef TINA_MATH_VECTOR2_HPP
#define TINA_MATH_VECTOR2_HPP

#include <glm/vec2.hpp>
#include <cmath>

namespace Tina
{
    class Vector2
    {
    public:
        union
        {
            struct
            {
                float x{};
                float y{};
            };

            glm::vec2 internalVec2;
        };

        Vector2(): internalVec2(0.0f, 0.0f)
        {
        }

        explicit Vector2(const glm::vec2& vec2): internalVec2(vec2)
        {
        }

        Vector2(const float x, const float y) : internalVec2(x, y)
        {
        }

        Vector2(const int x, const int y) : internalVec2(static_cast<float>(x), static_cast<float>(y))
        {
        }


        [[nodiscard]] bool isZero() const
        {
            return x == 0.0f && y == 0.0f;
        }

        [[nodiscard]] inline float length() const
        {
            return std::sqrt(x * x + y * y);
        }

        float& operator[](const int index)
        {
            return (&x)[index];
        }

        const float& operator[](const int index) const
        {
            return (&x)[index];
        }

        Vector2 operator*(const Vector2& other) const
        {
            return Vector2(internalVec2 * other.internalVec2);
        }

        Vector2 operator*(const float& other) const
        {
            return {internalVec2.x * other, internalVec2.y * other};
        }

        Vector2 operator+(const Vector2& other) const
        {
            return Vector2(internalVec2 + other.internalVec2);
        }

        Vector2 operator-(const Vector2& other) const
        {
            return Vector2(internalVec2 - other.internalVec2);
        }

        Vector2 operator/(const float& other) const
        {
            return {internalVec2.x / other, internalVec2.y / other};
        }

        bool operator==(const Vector2& other) const
        {
            return internalVec2 == other.internalVec2;
        }

        bool operator!=(const Vector2& other) const
        {
            return internalVec2 != other.internalVec2;
        }

        bool operator<(const Vector2& other) const
        {
            return x < other.x && y < other.y;
        }

        bool operator>(const Vector2& other) const
        {
            return x > other.x && y > other.y;
        }
    };
} // Tina

#endif //TINA_MATH_VECTOR2_HPP
