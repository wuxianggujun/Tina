#ifndef TINA_LEVEL_HPP
#define TINA_LEVEL_HPP

#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include "Layer.hpp"

namespace Tina {

    class Level {
    public:
        Level(std::string fileName);
        void updateGeometry();
        bool fail();

        std::vector<ColorVertex> vertices;
        std::vector<uint16_t> indices;

    private:
        void parseLevelFile();

        int error;
        std::ifstream levelFS;

        std::string name;
        int width;
        int hegiht;

        int attempts;

        double bestTime;

        std::vector<std::vector<int>> map;
    };
}


#endif
