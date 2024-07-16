//
// Created by wuxianggujun on 2024/7/15.
//

#ifndef TINA_MATH_RANDOM_HPP
#define TINA_MATH_RANDOM_HPP

#include <random>

namespace Tina
{
    class Random64
    {
    public:
        Random64(): twister_(std::random_device{}())
        {
        }

        unsigned long long operator()(unsigned long long min, unsigned long long max)
        {
            std::uniform_int_distribution<unsigned long long> distribution(min, max);
            return distribution(twister_);
        }

    private:
        std::mt19937_64 twister_;
    };
} // Tina

#endif //TINA_MATH_RANDOM_HPP
