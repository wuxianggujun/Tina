//
// Created by wuxianggujun on 2024/6/27.
//

#ifndef FLATPHYSICS_FLATPHYSICS_HPP
#define FLATPHYSICS_FLATPHYSICS_HPP

#include <string>

namespace Tina {
    struct FlatVector {
        float X;
        float Y;

        static constexpr FlatVector Zero() {
            return {0.0f, 0.0f};
        }

        constexpr FlatVector(float x, float y) : X(x), Y(y) {
        }

        constexpr FlatVector &operator+=(const FlatVector &other) {
            this->X += other.X;
            this->Y += other.Y;
            return *this;
        }

        FlatVector &operator-=(const FlatVector &other) {
            this->X -= other.X;
            this->Y -= other.Y;
            return *this;
        }


        constexpr FlatVector operator+(const FlatVector &other) const {
            return {this->X + other.X, this->Y + other.Y};
        }

        FlatVector operator-(const FlatVector &other) const {
            return {this->X - other.X, this->Y - other.Y};
        }

        // unary operator - 取反
        FlatVector operator-() const {
            return {-this->X, -this->Y};
        }

        constexpr FlatVector operator*(const FlatVector &other) const {
            return {this->X * other.X, this->Y * other.Y};
        }

        constexpr FlatVector operator*(const float &scalar) const {
            return {this->X * scalar, this->Y * scalar};
        }

        constexpr FlatVector &operator*=(const float &scalar) {
            this->X *= scalar;
            this->Y *= scalar;
            return *this;
        }

        constexpr FlatVector operator/(const FlatVector &other) const {
            return {this->X / other.X, this->Y / other.Y};
        }

        constexpr FlatVector operator/(const float &scalar) const {
            return {this->X / scalar, this->Y / scalar};
        }

        constexpr FlatVector &operator/=(const float &scalar) {
            this->X /= scalar;
            this->Y /= scalar;
            return *this;
        }


        bool operator==(const FlatVector &other) const {
            return this->X == other.X && this->Y == other.Y;
        }

        bool operator!=(const FlatVector &other) const {
            return !(*this == other);
        }

        [[nodiscard]] std::string toString() const {
            return "( X: " + std::to_string(X) + ", Y: " + std::to_string(Y) + ")";
        }
    };
} // Tina

#endif //FLATPHYSICS_FLATPHYSICS_HPP
