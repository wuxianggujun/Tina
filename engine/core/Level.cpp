#include "Level.hpp"

namespace Tina {

    Level::Level(std::string fileName) {
        levelFS.open(fileName);
        if (!levelFS.is_open())
        {
            error = 1;
            return;
        }
        
        parseLevelFile();
    }

    void Level::updateGeometry() {

    }

    void Level::parseLevelFile() {
        getline(levelFS, name);
        levelFS >> width;
        levelFS >> hegiht;
        levelFS >> attempts;
        levelFS >> bestTime;

        for (int row = 0;row < hegiht;row++)
        {
            std::vector<int> currentRow;
            for (int col = 0;col < width;col++)
            {
                int currentTile;

                levelFS >> currentTile;
                currentRow.push_back(currentTile);
                std::cout << currentTile << std::endl;
            }
            map.push_back(currentRow);
        }

        if (levelFS.fail())
        {
            error = 1;
            map.clear();
            return;
        }


    }

    bool Level::fail() {
        if (error)
        {
            return true;
        }
        return false;
    }



}
