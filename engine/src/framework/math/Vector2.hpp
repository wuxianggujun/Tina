//
// Created by wuxianggujun on 2024/5/31.
//

#ifndef TINA_FRAMEWORK_MATH_VECTOR_HPP
#define TINA_FRAMEWORK_MATH_VECTOR_HPP

#include "../Typedefs.hpp"

namespace Tina {

    class NO_DISCARD Vector2 {
    public:
        union {
            struct {
                union {
                    float x;
                    float width;
                };
                union {
                    float y;
                    float height;
                };
            };
            float coord[2] = {0};
        };

        inline float &operator[](int p_axis) {
            return coord[p_axis];
        }

        inline const float &operator[](int p_axis) const {
            return coord[p_axis];
        }

    };

} // Tina

#endif //TINA_FRAMEWORK_MATH_VECTOR_HPP
