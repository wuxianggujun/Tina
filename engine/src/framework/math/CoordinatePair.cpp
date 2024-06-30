//
// Created by wuxianggujun on 2024/6/30.
//

#include "CoordinatePair.hpp"

namespace Tina {
    void CoordinatePair::outputCoordinate() const {
        std::cout << "(" << x << ", " << y << ")" << std::endl;
    }

    CoordinatePair CoordinatePair::operator+(const CoordinatePair &other) const {
        return {this->x + other.x, this->y + other.y};
    }

    CoordinatePair CoordinatePair::operator+(float other) const {
        return {this->x + other, this->y + other};
    }

    CoordinatePair CoordinatePair::operator-(const CoordinatePair &other) const {
        return {this->x - other.x, this->y - other.y};
    }

    CoordinatePair CoordinatePair::operator-(float other) const {
        return {this->x - other, this->y - other};
    }

    CoordinatePair CoordinatePair::operator*(const CoordinatePair &other) const {
        return {this->x * other.x, this->y * other.y};
    }

    CoordinatePair CoordinatePair::operator*(float other) const {
        return {this->x * other, this->y * other};
    }

    CoordinatePair CoordinatePair::operator/(const CoordinatePair &other) const {
        return {this->x / other.x, this->y / other.y};
    }

    CoordinatePair CoordinatePair::operator/(float other) const {
        return {this->x / other, this->y / other};
    }
} // Tina
