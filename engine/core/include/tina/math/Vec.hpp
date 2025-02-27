#pragma once

#define GLM_ENABLE_EXPERIMENTAL  // 必须在包含任何 GLM 头文件之前定义
#include <glm/glm.hpp>
#include <glm/gtx/compatibility.hpp>
#include <type_traits>
#include <cmath>

namespace Tina::Math {

template<typename T, size_t N>
class Vec {
public:
    T data[N];

    // Default constructor
    constexpr Vec() : data{} {}

    // Scalar constructor
    explicit constexpr Vec(T scalar) {
        for (size_t i = 0; i < N; ++i) {
            data[i] = scalar;
        }
    }

    // Array constructor
    explicit constexpr Vec(const T* ptr) {
        for (size_t i = 0; i < N; ++i) {
            data[i] = ptr[i];
        }
    }

    // GLM vector constructor
    template<typename U>
    constexpr Vec(const glm::vec<N, U, glm::defaultp>& v) {
        for (size_t i = 0; i < N; ++i) {
            data[i] = static_cast<T>(v[i]);
        }
    }

    // Convert to GLM vector
    template<typename U = T>
    constexpr operator glm::vec<N, U, glm::defaultp>() const {
        glm::vec<N, U, glm::defaultp> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = static_cast<U>(data[i]);
        }
        return result;
    }

    // Array subscript operator
    constexpr T& operator[](size_t index) { return data[index]; }
    constexpr const T& operator[](size_t index) const { return data[index]; }

    // Vector operations
    constexpr Vec operator+(const Vec& rhs) const {
        Vec result;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] + rhs.data[i];
        }
        return result;
    }

    constexpr Vec operator-(const Vec& rhs) const {
        Vec result;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] - rhs.data[i];
        }
        return result;
    }

    constexpr Vec operator*(const Vec& rhs) const {
        Vec result;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] * rhs.data[i];
        }
        return result;
    }

    constexpr Vec operator/(const Vec& rhs) const {
        Vec result;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] / rhs.data[i];
        }
        return result;
    }

    // Scalar operations
    constexpr Vec operator*(T scalar) const {
        Vec result;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] * scalar;
        }
        return result;
    }

    constexpr Vec operator/(T scalar) const {
        Vec result;
        const T inv = T(1) / scalar;
        for (size_t i = 0; i < N; ++i) {
            result.data[i] = data[i] * inv;
        }
        return result;
    }

    // Compound assignment operators
    constexpr Vec& operator+=(const Vec& rhs) {
        for (size_t i = 0; i < N; ++i) {
            data[i] += rhs.data[i];
        }
        return *this;
    }

    constexpr Vec& operator-=(const Vec& rhs) {
        for (size_t i = 0; i < N; ++i) {
            data[i] -= rhs.data[i];
        }
        return *this;
    }

    constexpr Vec& operator*=(const Vec& rhs) {
        for (size_t i = 0; i < N; ++i) {
            data[i] *= rhs.data[i];
        }
        return *this;
    }

    constexpr Vec& operator/=(const Vec& rhs) {
        for (size_t i = 0; i < N; ++i) {
            data[i] /= rhs.data[i];
        }
        return *this;
    }

    // Utility functions
    constexpr T dot(const Vec& rhs) const {
        T result = T(0);
        for (size_t i = 0; i < N; ++i) {
            result += data[i] * rhs.data[i];
        }
        return result;
    }

    constexpr T lengthSquared() const {
        return dot(*this);
    }

    T length() const {
        return std::sqrt(lengthSquared());
    }

    Vec normalized() const {
        T len = length();
        if (len > T(0)) {
            return *this / len;
        }
        return *this;
    }
};

// Type aliases for common vector types
template<typename T> using Vec2 = Vec<T, 2>;
template<typename T> using Vec3 = Vec<T, 3>;
template<typename T> using Vec4 = Vec<T, 4>;

// Common type definitions
using Vec2f = Vec2<float>;
using Vec2d = Vec2<double>;
using Vec2i = Vec2<int>;

using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;
using Vec3i = Vec3<int>;

using Vec4f = Vec4<float>;
using Vec4d = Vec4<double>;
using Vec4i = Vec4<int>;

// Specialized constructors for Vec2
template<typename T>
class Vec<T, 2> {
public:
    union {
        T data[2];
        struct { T x, y; };
    };

    constexpr Vec() : x(T(0)), y(T(0)) {}
    constexpr Vec(T x_, T y_) : x(x_), y(y_) {}
    constexpr explicit Vec(T scalar) : x(scalar), y(scalar) {}
    
    template<typename U>
    constexpr Vec(const glm::vec<2, U, glm::defaultp>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)) {}

    template<typename U = T>
    constexpr operator glm::vec<2, U, glm::defaultp>() const {
        return glm::vec<2, U, glm::defaultp>(static_cast<U>(x), static_cast<U>(y));
    }
    
    // 添加相等运算符
    constexpr bool operator==(const Vec<T, 2>& rhs) const {
        return x == rhs.x && y == rhs.y;
    }
    
    // 添加不等运算符
    constexpr bool operator!=(const Vec<T, 2>& rhs) const {
        return !(*this == rhs);
    }
};

// Specialized constructors for Vec3
template<typename T>
class Vec<T, 3> {
public:
    union {
        T data[3];
        struct { T x, y, z; };
    };

    constexpr Vec() : x(T(0)), y(T(0)), z(T(0)) {}
    constexpr Vec(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec(T scalar) : x(scalar), y(scalar), z(scalar) {}
    
    template<typename U>
    constexpr Vec(const glm::vec<3, U, glm::defaultp>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)) {}

    template<typename U = T>
    constexpr operator glm::vec<3, U, glm::defaultp>() const {
        return glm::vec<3, U, glm::defaultp>(static_cast<U>(x), static_cast<U>(y), static_cast<U>(z));
    }

    constexpr Vec3<T> cross(const Vec3<T>& rhs) const {
        return Vec3<T>(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }
    
    // 添加相等运算符
    constexpr bool operator==(const Vec<T, 3>& rhs) const {
        return x == rhs.x && y == rhs.y && z == rhs.z;
    }
    
    // 添加不等运算符
    constexpr bool operator!=(const Vec<T, 3>& rhs) const {
        return !(*this == rhs);
    }
};

// Specialized constructors for Vec4
template<typename T>
class Vec<T, 4> {
public:
    union {
        T data[4];
        struct { T x, y, z, w; };
    };

    constexpr Vec() : x(T(0)), y(T(0)), z(T(0)), w(T(0)) {}
    constexpr Vec(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr explicit Vec(T scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    
    template<typename U>
    constexpr Vec(const glm::vec<4, U, glm::defaultp>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)), w(static_cast<T>(v.w)) {}

    template<typename U = T>
    constexpr operator glm::vec<4, U, glm::defaultp>() const {
        return glm::vec<4, U, glm::defaultp>(static_cast<U>(x), static_cast<U>(y), static_cast<U>(z), static_cast<U>(w));
    }
    
    // 添加相等运算符
    constexpr bool operator==(const Vec<T, 4>& rhs) const {
        return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
    }
    
    // 添加不等运算符
    constexpr bool operator!=(const Vec<T, 4>& rhs) const {
        return !(*this == rhs);
    }
};

} // namespace Tina::Math 