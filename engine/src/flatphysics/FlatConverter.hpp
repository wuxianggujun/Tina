//
// Created by wuxianggujun on 2024/6/30.
//

#ifndef FLATCONVERTER_HPP
#define FLATCONVERTER_HPP

#include "flatphysics/FlatVector.hpp"
#include "framework/math/Vector2.hpp"

namespace Tina {
    class FlatConverter {
    public:
         static Vector2 toVector2(const FlatVector &vector) {
            return {vector.X, vector.Y};
        }
    };
} // Tina

#endif //FLATCONVERTER_HPP
