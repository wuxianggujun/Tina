//
// Created by wuxianggujun on 2024/7/17.
//

#ifndef VECTOR_HPP
#define VECTOR_HPP

#include <cmath>
#include <format>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/vec2.hpp>

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

            glm::vec<2, T, glm::defaultp> internalVec2;
        };

        Vector() : internalVec2(0, 0)
        {
        }

        Vector(T x, T y) : internalVec2(x, y)
        {
        }

        explicit Vector(const glm::vec<2, T, glm::defaultp>& vec2) : internalVec2(vec2)
        {
        }

        [[nodiscard]] std::string toString() const
        {
            return std::format("[ x: %s, y: %s ]", this->x, this->y);
        }

        void toStdout() const
        {
            std::cout << this->toString() << std::endl;
        }
    };

    using Vector2f = Vector<float>;
    using Vector2i = Vector<int>;
    using Vector2d = Vector<double>;
} // Tina

#endif //VECTOR_HPP
