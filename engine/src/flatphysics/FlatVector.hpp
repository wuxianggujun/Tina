//
// Created by wuxianggujun on 2024/6/27.
//

#ifndef FLATPHYSICS_HPP
#define FLATPHYSICS_HPP

namespace Tina {
    struct FlatVector {
        const float X;
        const float Y;

        FlatVector(float x, float y) : X(x), Y(y) {
        }
    };

    static FlatVector operator+(const FlatVector &a, const FlatVector &b) {
        return FlatVector(a.X + b.X, a.Y + b.Y);
    }
} // Tina

#endif //FLATPHYSICS_HPP
