#include "Player.hpp"

namespace Tina {
    uint32_t playerColor = 0xffff00ff;

    Player::Player(Level *level) {
        this->level = level;
        this->size = (*level).tileSize;
        this->pos = level->startingPoint;
        this->velocity = {0, 0};
        this->vertices = {
            {pos.x - (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y + (size / 2), playerColor},
            {pos.x - (size / 2), pos.y + (size / 2), playerColor}
        };
        this->indices = {
            0, 1, 2,
            2, 3, 0
        };
    }

    void Player::SetLevel(Level *level) {
        this->level = level;
        this->size = (*level).tileSize;
        this->pos = level->startingPoint;
        this->vertices = {
            {pos.x - (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y + (size / 2), playerColor},
            {pos.x - (size / 2), pos.y + (size / 2), playerColor}
        };

        this->velocity = {0, 0};
    }

    void Player::SetSize(float size) {
        this->vertices = {
            {pos.x - (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y - (size / 2), playerColor},
            {pos.x + (size / 2), pos.y + (size / 2), playerColor},
            {pos.x - (size / 2), pos.y + (size / 2), playerColor}
        };
        UpdateGeometry();
    }

    void Player::SetLayer(Layer *layer) {
        this->layer = layer;
        this->layer->updateGeometry(this->vertices, this->indices);
    }

    void Player::SetPosition(CoordinatePair newPos) {
        CoordinatePair oldPos = this->pos;
        this->pos = newPos;
        CoordinatePair posChange = this->pos - oldPos;
        for (int i = 0; i < this->vertices.size(); i++) {
            this->vertices.at(i).xPos += posChange.x;
            this->vertices.at(i).yPos += posChange.y;
        }
        UpdateGeometry();
    }

    // Requirement 1: Algorithm
    bool Player::IsColliding(CoordinatePair playerPos) {
        // Requirement 3: Input
        float halfPlayer = size / 2; // Requirement 4: Variable
        std::vector<CoordinatePair> corners{
            // Requirement 5: Array
            (playerPos + CoordinatePair(halfPlayer, halfPlayer)),
            (playerPos + CoordinatePair(halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, halfPlayer))
        };

        for (int i = 0; i < corners.size(); i++) {
            // Requirement 7: Iteration
            if (level->posToData(corners.at(i)) == 1) {
                // Requirement 9: Control
                return true; // Requirement 3: Output
            }
        }
        return false; // Requirement 3: Output
    }

    // Requirement 1: Algorithm
    CoordinatePair Player::CorrectCollision(CoordinatePair playerPos) {
        CoordinatePair offset = {0, 0};
        float halfPlayer = size / 2;
        float tileSize = level->tileSize;
        bool groundCheck = false;

        std::vector<CoordinatePair> corners{
            (playerPos + CoordinatePair(halfPlayer, halfPlayer)),
            (playerPos + CoordinatePair(halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, halfPlayer)),
        };
        if (level->posToData(corners.at(0)) == 1) {
            // Top Right Collision
            CoordinatePair tileIndex = level->posToIndex(corners.at(0));
            CoordinatePair tilePos = {tileIndex.x * tileSize - 1, (tileIndex.y + 1) * tileSize - 1};
            CoordinatePair overlap = {abs(corners.at(0).x - tilePos.x), abs(corners.at(0).y + tilePos.y)};
            if (overlap.x < overlap.y) {
                offset.x -= overlap.x;
                velocity.x = 0;
            } else {
                offset.y -= overlap.y;
                velocity.y = 0;
            }
        }
        if (level->posToData(corners.at(1)) == 1) {
            // Bottom Right Collision
            CoordinatePair tileIndex = level->posToIndex(corners.at(1));
            CoordinatePair tilePos = {tileIndex.x * tileSize - 1, tileIndex.y * tileSize - 1};
            CoordinatePair overlap = {abs(corners.at(1).x - tilePos.x), abs(corners.at(1).y + tilePos.y)};
            if (overlap.x < overlap.y) {
                offset.x -= overlap.x;
                velocity.x = 0;
            } else {
                offset.y += overlap.y;
                velocity.y = 0;
                groundCheck = true;
            }
        }
        if (level->posToData(corners.at(2)) == 1) {
            // Bottom Left Collision
            CoordinatePair tileIndex = level->posToIndex(corners.at(2));
            CoordinatePair tilePos = {(tileIndex.x + 1) * tileSize - 1, tileIndex.y * tileSize - 1};
            CoordinatePair overlap = {abs(corners.at(2).x - tilePos.x), abs(corners.at(2).y + tilePos.y)};
            if (overlap.x < overlap.y) {
                offset.x += overlap.x;
                velocity.x = 0;
            } else {
                offset.y += overlap.y;
                velocity.y = 0;
                groundCheck = true;
            }
        }
        if (level->posToData(corners.at(3)) == 1) {
            // Top Left Collision
            CoordinatePair tileIndex = level->posToIndex(corners.at(2));
            CoordinatePair tilePos = {(tileIndex.x + 1) * tileSize - 1, (tileIndex.y + 1) * tileSize - 1};
            CoordinatePair overlap = {abs(corners.at(2).x - tilePos.x), abs(corners.at(2).y + tilePos.y)};
            if (overlap.x < overlap.y) {
                offset.x += overlap.x;
                velocity.x = 0;
            } else {
                offset.y -= overlap.y;
                velocity.y = 0;
            }
        }
        this->onGround = groundCheck;
        return offset;
    }

    void Player::UpdatePosition(float deltaTime) {
        float halfPlayer = size / 2;

        CoordinatePair posChange = {velocity.x, velocity.y};
        posChange = posChange * (deltaTime * size);
        CoordinatePair newPos = pos + posChange;

        newPos = newPos + CorrectCollision(newPos);

        if (CheckDeath(newPos)) {
            level->nextAttempt();
            newPos = level->startingPoint;
        }

        CheckWin(newPos);
        SetPosition(newPos);
        UpdateGeometry();
    }

    void Player::SetVelocity(CoordinatePair newVelocity) {
        this->velocity = newVelocity;
    }

    void Player::ChangeVelocity(CoordinatePair velocityChange) {
        this->velocity.x = this->velocity.x + velocityChange.x;
        this->velocity.y = this->velocity.y + velocityChange.y;
    }

    bool Player::CheckWin(CoordinatePair playerPos) {
        if (nextLevel) return false;
        float halfPlayer = size / 2;
        std::vector<CoordinatePair> corners{
            (playerPos + CoordinatePair(halfPlayer, halfPlayer)),
            (playerPos + CoordinatePair(halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, halfPlayer)),
        };
        if (level->posToData(playerPos) == 3) {
            nextLevel = true;
            level->completedLevel();
            return true;
        }
        for (int i = 0; i < corners.size(); i++) {
            if (level->posToData(corners.at(i)) == 3) {
                nextLevel = true;
                level->completedLevel();
                return true;
            }
        }
        return false;
    }

    bool Player::CheckDeath(CoordinatePair playerPos) {
        float halfPlayer = size / 2;
        std::vector<CoordinatePair> corners{
            (playerPos + CoordinatePair(halfPlayer, halfPlayer)),
            (playerPos + CoordinatePair(halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, -halfPlayer)),
            (playerPos + CoordinatePair(-halfPlayer, halfPlayer)),
        };
        if (level->posToData(playerPos) == 4) {
            return true;
        }
        /*
        if ((level->PosToData(corners.at(1)) == 4 && level->PosToData(corners.at(2)) == 4) ||
            (level->PosToData(corners.at(1)) == 4 && level->PosToData(corners.at(2)) == 4) ||
            (level->PosToData(corners.at(1)) == 4 && level->PosToData(corners.at(2)) == 4) ||
            (level->PosToData(corners.at(1)) == 4 && level->PosToData(corners.at(2)) == 4)) {
          return true;
        }
        */
        for (int i = 0; i < corners.size(); i++) {
            if (level->posToData(corners.at(i)) == 4) {
                return true;
            }
        }
        return false;
    }

    void Player::UpdateVelocity(float deltaTime) {
        // friction
        if (velocity.x > 0) {
            velocity.x *= 0.90;
        } else if (velocity.x < 0) {
            velocity.x *= 0.90;
        }
        if (velocity.y > 0) {
            velocity.y -= 4 * size;
        } else if (velocity.y < 0) {
            velocity.y -= 4 * size;
        }

        if (velocity.x > 0 && abs(velocity.x) > maxVel) {
            velocity.x = maxVel;
        }
        if (velocity.x < 0 && abs(velocity.x) > maxVel) {
            velocity.x = 0 - maxVel;
        }

        // gravity
        if (onGround == false) {
            velocity.y -= 0.05;
        }

        if (abs(velocity.x) < 0.01) {
            velocity.x = 0;
        }
        if (abs(velocity.y) < 0.01) {
            velocity.y = 0;
        }
    }

    void Player::UpdateGeometry() {
        this->layer->updateGeometry(this->vertices, this->indices);
    }
}
