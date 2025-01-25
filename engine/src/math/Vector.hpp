//
// Created by wuxianggujun on 2024/7/17.
//

#pragma once

#include <cmath>
// If the gcc compiler is used on a Linux system, check whether <format>header files exist
/*
#if __has_include(<format>)
#include <format>
#endif

#ifdef __cpp_lib_format

#else

#endif
*/
#include <fmt/format.h>
#include <iostream>
#include <sstream>
#include <string>
#include <glm/glm.hpp>
#include <glm/vec2.hpp>

namespace Tina
{
    template <class T>
    class Vector
    {
    public:
        union
        {
            struct
            {
                union
                {
                    T x;
                    T width;
                };

                union
                {
                    T y;
                    T height;
                };
            };

            glm::vec<2, T, glm::defaultp> internalVec;
        };

        Vector() : internalVec(0, 0)
        {
        }

        Vector(T x, T y) : internalVec(x, y)
        {
        }

        explicit Vector(const glm::vec<2, T, glm::defaultp>& vec2) : internalVec(vec2)
        {
        }

        T& operator[](const int index)
        {
            return (&x)[index];
        }

        const T& operator[](const int index) const
        {
            return (&x)[index];
        }

        Vector<T> operator+(const Vector<T>& other) const
        {
            return Vector<T>(internalVec + other.internalVec);
        }

        Vector<T> operator+(const T& other) const
        {
            return Vector<T>(internalVec.x + other, internalVec.y + other);
        }

        Vector<T> operator-(const Vector<T>& other) const
        {
            return Vector<T>(internalVec - other.internalVec);
        }

        Vector<T> operator-(const T& other) const
        {
            return Vector<T>(internalVec.x - other, internalVec.y - other);
        }

        Vector<T> operator*(const Vector<T>& other) const
        {
            return Vector<T>(internalVec * other.internalVec);
        }


        Vector<T> operator*(const T& other) const
        {
            return Vector<T>(internalVec.x * other, internalVec.y * other);
        }

        Vector<T> operator/(const Vector<T>& other) const
        {
            return Vector<T>(internalVec / other.internalVec);
        }

        Vector<T> operator/(const T& other) const
        {
            return Vector<T>(internalVec.x / other, internalVec.y / other);
        }

        Vector<T>& operator+=(const Vector<T>& other)
        {
            internalVec += other.internalVec;
            return *this;
        }

        Vector<T>& operator+=(const T& other)
        {
            internalVec.x += other;
            internalVec.y += other;
            return *this;
        }

        bool operator==(const Vector& other) const
        {
            return internalVec == other.internalVec;
        }


        bool operator!=(const Vector& other) const
        {
            return internalVec != other.internalVec;
        }


        Vector<T>& operator-=(const Vector<T>& other)
        {
            internalVec -= other.internalVec;
            return *this;
        }

        Vector<T>& operator-=(const T& other)
        {
            internalVec.x -= other;
            internalVec.y -= other;
            return *this;
        }

        Vector<T>& operator*=(const Vector<T>& other)
        {
            internalVec *= other.internalVec;
            return *this;
        }

        bool operator<(const Vector<T>& other) const
        {
            return internalVec.x < other.internalVec.x && internalVec.y < other.internalVec.y;
        }

        bool operator>(const Vector<T>& other) const
        {
            return internalVec.x > other.internalVec.x && internalVec.y > other.internalVec.y;
        }

        bool isZero() const
        {
            return x == 0 && y == 0;
        }

        bool length() const
        {
            return std::sqrt(x * x + y * y);
        }

        [[nodiscard]] std::string toString() const
        {
            return fmt::format("[ x: {}, y: {} ]", this->x, this->y);
        }

        void toStdout() const
        {
            std::cout << this->toString() << std::endl;
        }
    };

    using Vector2f = Vector<float>;
    using Vector2i = Vector<int>;
    using Vector2d = Vector<double>;

    struct Vector2 {
        float x, y;

        Vector2() : x(0.0f), y(0.0f) {}
        Vector2(float x, float y) : x(x), y(y) {}

        Vector2 operator+(const Vector2& other) const {
            return Vector2(x + other.x, y + other.y);
        }

        Vector2 operator-(const Vector2& other) const {
            return Vector2(x - other.x, y - other.y);
        }

        Vector2 operator*(float scalar) const {
            return Vector2(x * scalar, y * scalar);
        }

        Vector2 normalized() const {
            float length = sqrt(x * x + y * y);
            if (length != 0.0f) {
                return Vector2(x / length, y / length);
            }
            return *this;
        }
    };
} // Tina
