//
// Created by wuxianggujun on 2024/6/30.
//

#ifndef FRAMEWORK_LEVEL_HPP
#define FRAMEWORK_LEVEL_HPP

#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include "Layer.hpp"
#include "math/CoordinatePair.hpp"

namespace Tina {

class Level {
public:
    explicit Level(std::string fileName);

    ~Level();

    void updateGeometry();
    void setLayer(Layer* layer);
    bool fail();

    int width;
    int height;

    std::vector<ColorVertex> vertices;
    std::vector<uint16_t> indices;
    float tileSize;

    CoordinatePair posToIndex(CoordinatePair pos);
    int getMapData(int x, int y);
    int getMapData(CoordinatePair pos);
    int posToData(CoordinatePair pos);
    void completedLevel();
    void nextAttempt();

    CoordinatePair startingPoint;

private:
    static const std::vector<uint32_t> colors;
    void parseLevelFile();
    void writeLevelFile();

    int error;
    std::ifstream levelFS;
    std::string fileName;
    std::vector<std::string> files;
    int currentLevel;

    Layer* layer;

    std::string name;

    CoordinatePair offset = {0, 0};

    int attempts;

    std::vector<std::vector<int>> map;
};

} // Tina

#endif //FRAMEWORK_LEVEL_HPP
