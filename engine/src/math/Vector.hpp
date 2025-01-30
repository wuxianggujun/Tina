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
#include <glm/vec3.hpp>

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

        bool operator>=(const Vector<T>& other) const {
            return glm::all(glm::greaterThanEqual(internalVec, other.internalVec));
        }

        bool operator<=(const Vector<T>& other) const {
            return glm::all(glm::lessThanEqual(internalVec, other.internalVec));
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

    template <class T>
    class Vector3
    {
    public:
        union
        {
            struct
            {
                T x;
                T y;
                T z;
            };
            glm::vec<3, T, glm::defaultp> internalVec;
        };

        Vector3() : internalVec(0, 0, 0) {}
        Vector3(T x, T y, T z) : internalVec(x, y, z) {}
        explicit Vector3(const glm::vec<3, T, glm::defaultp>& vec3) : internalVec(vec3) {}

        T& operator[](const int index)
        {
            return (&x)[index];
        }

        const T& operator[](const int index) const
        {
            return (&x)[index];
        }

        Vector3<T> operator+(const Vector3<T>& other) const
        {
            return Vector3<T>(internalVec + other.internalVec);
        }

        Vector3<T> operator-(const Vector3<T>& other) const
        {
            return Vector3<T>(internalVec - other.internalVec);
        }

        Vector3<T> operator*(const T& scalar) const
        {
            return Vector3<T>(internalVec * scalar);
        }

        Vector3<T> operator/(const T& scalar) const
        {
            return Vector3<T>(internalVec / scalar);
        }

        bool operator==(const Vector3<T>& other) const
        {
            return internalVec == other.internalVec;
        }

        bool operator!=(const Vector3<T>& other) const
        {
            return internalVec != other.internalVec;
        }

        T length() const
        {
            return std::sqrt(x * x + y * y + z * z);
        }

        Vector3<T> normalized() const
        {
            T len = length();
            if (len != 0)
            {
                return Vector3<T>(x / len, y / len, z / len);
            }
            return *this;
        }

        [[nodiscard]] std::string toString() const
        {
            return fmt::format("[ x: {}, y: {}, z: {} ]", x, y, z);
        }

        void toStdout() const
        {
            std::cout << toString() << std::endl;
        }
    };

    using Vector3f = Vector3<float>;
    using Vector3i = Vector3<int>;
    using Vector3d = Vector3<double>;
} // Tina
