//
// Created by wuxianggujun on 2024/6/30.
//

#include "Level.hpp"

namespace Tina {
    /*
     * Level file (.lf)
     * ------------
     * levelName
     * width height
     * attempts
     *
     * map (ex. 1 0 0
     *          1 1 1
     *          0 1 0)
     */


    const std::vector<uint32_t> Level::colors{
        0xdddddddd,
        0x55555555,
        0x0000dd00,
        0xffff5500,
        0x000077ff
    };


    Level::Level(std::string filename) {
        this->fileName = filename;
        levelFS.open(filename);
        if (!levelFS.is_open()) {
            error = 1;
            return;
        }

        parseLevelFile();
        nextAttempt();
    }

    Level::~Level() {
        levelFS.close();
        vertices.clear();
        indices.clear();
    }

    void Level::updateGeometry() {
        if (map.size() == 0) {
            return;
        }
        tileSize = 2.0 / std::max(width, height);

        if (width > height) {
            offset.y = ((width - height) * tileSize) / 2;
        }
        if (height > width) {
            offset.x = ((height - width) * tileSize) / 2;
        }

        //cout << "Tile size: " << tileSize << endl;

        std::vector<ColorVertex> currentVertices{
            {-1.0f + offset.x, 1.0f - offset.y, colors.at(1)},
            {-1.0f + tileSize + offset.x, 1.0f - offset.y, colors.at(1)},
            {-1.0f + tileSize + offset.x, 1.0f - tileSize - offset.y, colors.at(1)},
            {-1.0f + offset.x, 1.0f - tileSize - offset.y, colors.at(1)}
        };

        std::vector<uint16_t> currentIndices{
            0, 1, 2,
            3, 2, 0
        };

        this->vertices.clear();
        this->indices.clear();

        for (int y = 0; y < map.size(); y++) {
            for (int x = 0; x < map.at(0).size(); x++) {
                //cout << map.at(y).at(x) << " ";
                if (map.at(y).at(x) != 0) {
                    if (map.at(y).at(x) == 2) {
                        startingPoint = {
                            (x - width / 2.0f) * tileSize + tileSize / 2,
                            -((y - height / 2.0f) * tileSize + tileSize / 2)
                        };
                    }
                    for (int i = 0; i < currentVertices.size(); i++) {
                        currentVertices.at(i).color = colors.at(map.at(y).at(x));
                    }
                    for (int i = 0; i < currentVertices.size(); i++) {
                        this->vertices.push_back(currentVertices.at(i));
                    }
                    for (int i = 0; i < currentIndices.size(); i++) {
                        this->indices.push_back(currentIndices.at(i));
                        currentIndices.at(i) += currentVertices.size();
                    }
                }
                for (int i = 0; i < currentVertices.size(); i++) {
                    currentVertices.at(i).xPos += tileSize;
                }
            }
            for (int i = 0; i < currentVertices.size(); i++) {
                currentVertices.at(i).yPos -= tileSize;
                currentVertices.at(i).xPos -= (tileSize * width);
            }
            //cout << endl;
        }
    }

    // Requirement 6: File Input
    void Level::parseLevelFile() {
        getline(levelFS, name);
        levelFS >> width;
        levelFS >> height;
        levelFS >> attempts;

        std::cout << name << std::endl;

        //Populates map vector
        std::vector<int> currentRow;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int currentTile;
                levelFS >> currentTile;
                currentRow.push_back(currentTile);
                if (levelFS.fail()) {
                    std::cout << "Read Failure: ( x: " << x << ", y: " << y << " )" << std::endl;
                }
            }
            this->map.push_back(currentRow);
            currentRow.clear();
        }
        //cout << "Map size: " << map.at(0).size() << " x " << map.size() << endl;

        if (levelFS.fail()) {
            std::cout << "Level load error" << std::endl;
            error = 1;
            return;
        }
        levelFS.close();
    }

    void Level::setLayer(Layer* layer) {
        this->layer = layer;

        updateGeometry();

        if (this->vertices.size() != 0 && this->indices.size() != 0) {
            this->layer->updateGeometry(this->vertices, this->indices);
        } else {
            std::cout << "No data in vertices or indices vectors" << std::endl;
        }
    }

    bool Level::fail() {
        if (error) {
            return true;
        }
        return false;
    }

    CoordinatePair Level::posToIndex(CoordinatePair pos) {
        CoordinatePair index{};

        index.x = (int) round(((pos.x + 1 + tileSize / 2) / 2) * (width - 1) * (1 + 1.0 / (width - 1)) - 1);
        index.y = height - (int) round(((pos.y + 1 + tileSize / 2) / 2) * (height - 1) * (1 + 1.0 / (height - 1)));


        if (index.x == -0 || index.x < 0) index.x = 0;
        if (index.y == -0 || index.x < 0) index.y = 0;
        if (index.x > width - 1) index.x = width - 1;
        if (index.y > height - 1) index.y = height - 1;
        /*
        cout << "Pos:  ( x: " << pos.x << ", y: " << pos.y << " )" << endl;
        cout << "Index:( x: " << index.x << ", y: " << index.y << " )" << endl;
        */
        return index;
    }

    int Level::getMapData(int x, int y) {
        return map[y][x];
    }

    int Level::getMapData(CoordinatePair pos) {
        return map[pos.y][pos.x];
    }

    int Level::posToData(CoordinatePair pos) {
        return getMapData(posToIndex(pos));
    }

    void Level::completedLevel() {
        writeLevelFile();
    }

    void Level::nextAttempt() {
        attempts++;
        std::cout << "Attempt " << attempts << std::endl;
    }

    //Requirement 6: File Output
    void Level::writeLevelFile() {
        std::ofstream outFS;
        outFS.open(fileName);

        if (!outFS.is_open()) {
            std::cout << "file write error" << std::endl;
            return;
        }

        //Clears file
        outFS << "";

        outFS << name << "\n";
        outFS << width << " " << height << "\n";
        outFS << attempts << "\n\n";

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                outFS << map.at(y).at(x) << " ";
            }
            outFS << "\n";
        }
        outFS.close();
    }
} // Tina
