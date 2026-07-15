#pragma once

#include "Core.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Tina {
    namespace Math {
        // Base vector types from GLM
        using Vec2 = glm::vec2;
        using Vec3 = glm::vec3;
        using Vec4 = glm::vec4;
        
        using IVec2 = glm::ivec2;
        using IVec3 = glm::ivec3;
        using IVec4 = glm::ivec4;
        
        using UVec2 = glm::uvec2;
        using UVec3 = glm::uvec3;
        using UVec4 = glm::uvec4;
        
        // Convenience aliases for common use cases
        using Point = IVec2;      // 2D point with integer coordinates
        using Size = IVec2;       // 2D size with integer dimensions
        using Position = Vec2;    // 2D position with float coordinates
        
        // Rectangle structure using GLM vectors
        struct TINA_CORE_API Rect
        {
            Point position;       // Top-left corner
            Size size;           // Width and height
            
            Rect() = default;
            Rect(i32 x, i32 y, i32 w, i32 h) : position(x, y), size(w, h) {}
            Rect(const Point& pos, const Size& sz) : position(pos), size(sz) {}
            
            i32 x() const { return position.x; }
            i32 y() const { return position.y; }
            i32 width() const { return size.x; }
            i32 height() const { return size.y; }
            
            i32 right() const { return position.x + size.x; }
            i32 bottom() const { return position.y + size.y; }
            
            Point topLeft() const { return position; }
            Point topRight() const { return Point(right(), y()); }
            Point bottomLeft() const { return Point(x(), bottom()); }
            Point bottomRight() const { return Point(right(), bottom()); }
            
            bool contains(const Point& point) const
            {
                return point.x >= x() && point.x < right() && 
                       point.y >= y() && point.y < bottom();
            }
            
            bool intersects(const Rect& other) const
            {
                return !(right() <= other.x() || x() >= other.right() ||
                        bottom() <= other.y() || y() >= other.bottom());
            }
        };
        
        // Matrix types
        using Mat2 = glm::mat2;
        using Mat3 = glm::mat3;
        using Mat4 = glm::mat4;
        
        using Mat2x3 = glm::mat2x3;
        using Mat2x4 = glm::mat2x4;
        using Mat3x2 = glm::mat3x2;
        using Mat3x4 = glm::mat3x4;
        using Mat4x2 = glm::mat4x2;
        using Mat4x3 = glm::mat4x3;
        
        // Quaternion
        using Quat = glm::quat;
        
        // Common constants（不导出为 DLL 符号，避免 MSVC 对 dllimport 的限制）
        inline constexpr float PI = glm::pi<float>();
        inline constexpr float TWO_PI = 2.0f * PI;
        inline constexpr float HALF_PI = PI * 0.5f;
        inline constexpr float DEG_TO_RAD = PI / 180.0f;
        inline constexpr float RAD_TO_DEG = 180.0f / PI;
        
        // Common functions
        template<typename T>
        constexpr T radians(T degrees) {
            return glm::radians(degrees);
        }
        
        template<typename T>
        constexpr T degrees(T radians) {
            return glm::degrees(radians);
        }
        
        template<typename T>
        constexpr T sin(T angle) {
            return glm::sin(angle);
        }
        
        template<typename T>
        constexpr T cos(T angle) {
            return glm::cos(angle);
        }
        
        template<typename T>
        constexpr T tan(T angle) {
            return glm::tan(angle);
        }
        
        template<typename T>
        constexpr T abs(T x) {
            return glm::abs(x);
        }
        
        template<typename T>
        constexpr T min(T a, T b) {
            return glm::min(a, b);
        }
        
        template<typename T>
        constexpr T max(T a, T b) {
            return glm::max(a, b);
        }
        
        template<typename T>
        constexpr T clamp(T x, T minVal, T maxVal) {
            return glm::clamp(x, minVal, maxVal);
        }
        
        template<typename T>
        constexpr T mix(T x, T y, T a) {
            return glm::mix(x, y, a);
        }
        
        template<typename T>
        constexpr T step(T edge, T x) {
            return glm::step(edge, x);
        }
        
        template<typename T>
        constexpr T smoothstep(T edge0, T edge1, T x) {
            return glm::smoothstep(edge0, edge1, x);
        }
        
        // Vector operations
        template<typename T>
        constexpr T dot(const T& x, const T& y) {
            return glm::dot(x, y);
        }
        
        template<typename T>
        constexpr T cross(const T& x, const T& y) {
            return glm::cross(x, y);
        }
        
        template<typename T>
        constexpr auto length(const T& x) {
            return glm::length(x);
        }
        
        template<typename T>
        constexpr T normalize(const T& x) {
            return glm::normalize(x);
        }
        
        template<typename T>
        constexpr auto distance(const T& p0, const T& p1) {
            return glm::distance(p0, p1);
        }
        
        template<typename T>
        constexpr T reflect(const T& I, const T& N) {
            return glm::reflect(I, N);
        }
        
        template<typename T>
        constexpr T refract(const T& I, const T& N, typename T::value_type eta) {
            return glm::refract(I, N, eta);
        }
        
        // Matrix operations
        template<typename T>
        constexpr T transpose(const T& m) {
            return glm::transpose(m);
        }
        
        template<typename T>
        constexpr auto determinant(const T& m) {
            return glm::determinant(m);
        }
        
        template<typename T>
        constexpr T inverse(const T& m) {
            return glm::inverse(m);
        }
        
        // Transformation matrices
        TINA_CORE_API inline Mat4 translate(const Mat4& m, const Vec3& v) {
            return glm::translate(m, v);
        }
        
        TINA_CORE_API inline Mat4 rotate(const Mat4& m, float angle, const Vec3& axis) {
            return glm::rotate(m, angle, axis);
        }
        
        TINA_CORE_API inline Mat4 scale(const Mat4& m, const Vec3& v) {
            return glm::scale(m, v);
        }
        
        TINA_CORE_API inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
            return glm::lookAt(eye, center, up);
        }
        
        TINA_CORE_API inline Mat4 perspective(float fovy, float aspect, float zNear, float zFar) {
            return glm::perspective(fovy, aspect, zNear, zFar);
        }
        
        TINA_CORE_API inline Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
            return glm::ortho(left, right, bottom, top, zNear, zFar);
        }
        
        // Quaternion operations
        TINA_CORE_API inline Quat angleAxis(float angle, const Vec3& axis) {
            return glm::angleAxis(angle, axis);
        }
        
        TINA_CORE_API inline Mat4 toMat4(const Quat& q) {
            // gtc/quaternion 提供 mat4_cast 代替 gtx 的 toMat4
            return glm::mat4_cast(q);
        }
        
        TINA_CORE_API inline Quat slerp(const Quat& x, const Quat& y, float a) {
            return glm::slerp(x, y, a);
        }
        
        // Pointer access
        template<typename T>
        constexpr auto value_ptr(const T& v) {
            return glm::value_ptr(v);
        }
    }
}
