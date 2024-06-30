//
// Created by wuxianggujun on 2024/6/30.
//

#ifndef MATH_COORDINATEPAIR_HPP
#define MATH_COORDINATEPAIR_HPP

#include <iostream>

namespace Tina {
    class CoordinatePair {
    public:
        float x;
        float y;

        CoordinatePair():x(0),y(0){};

        CoordinatePair(float x, float y):x(x),y(y){};

        void outputCoordinate() const;

        CoordinatePair operator+(const CoordinatePair &other) const;

        CoordinatePair operator+(float other) const;

        CoordinatePair operator-(const CoordinatePair &other) const;

        CoordinatePair operator-(float other) const;

        CoordinatePair operator*(const CoordinatePair &other) const;

        CoordinatePair operator*(float other) const;

        CoordinatePair operator/(const CoordinatePair &other) const;

        CoordinatePair operator/(float other) const;
    };
} // Tina

#endif //MATH_COORDINATEPAIR_HPP
