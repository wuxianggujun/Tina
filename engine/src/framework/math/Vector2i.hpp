//
// Created by wuxianggujun on 2024/5/31.
//

#ifndef TINA_FRAMEWORK_MATH_VECTORI_HPP
#define TINA_FRAMEWORK_MATH_VECTORI_HPP

#include <cstdint>
#include "../Typedefs.hpp"

namespace Tina {
    class NO_DISCARD Vector2i {
    public:
        union {
            struct {
                union {
                    int32_t x;
                    int32_t width;
                };

                union {
                    int32_t y;
                    int32_t height;
                };
            };

            int32_t coord[2] = {0};
        };

        inline int32_t &operator[](int p_axis) {
            return coord[p_axis];
        }

        inline const int32_t &operator[](int p_axis) const {
            return coord[p_axis];
        }
    };
} // Tina

#endif //TINA_FRAMEWORK_MATH_VECTORI_HPP
