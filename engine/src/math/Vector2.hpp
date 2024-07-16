//
// Created by wuxianggujun on 2024/7/15.
//

#ifndef TINA_MATH_VECTOR2_HPP
#define TINA_MATH_VECTOR2_HPP

#include <glm/vec2.hpp>

namespace Tina
{
    class Vector2
    {
    public:
        union
        {
            struct
            {
                float x;
                float y;
            };
            glm::vec2 internalVec2;
        };

        

        
    };
} // Tina

#endif //TINA_MATH_VECTOR2_HPP
