#ifndef PLAYER_H_HEADER_GUARD
#define PLAYER_H_HEADER_GUARD

#include <vector>

#include "math/CoordinatePair.hpp"
#include "Layer.hpp"
#include "Level.hpp"

namespace Tina {
    class Player {
    public:
        Player(Level *level); // Constructor
        void SetLevel(Level *level); // Sets the level for the player to interact with
        void SetLayer(Layer *layer); // Sets the layer for the player to be rendered to
        void SetSize(float size); // Sets the size of the player

        // Position
        void SetPosition(CoordinatePair newPos); // Sets the player's position and updates screen
        void UpdatePosition(float deltaTime); // Uses velocity values to update position
        // Velocity
        void SetVelocity(CoordinatePair newVelocity); // Sets the player's velocity
        void ChangeVelocity(CoordinatePair velocityChange); // Changes the player's velocity
        void UpdateVelocity(float deltaTime); // Changes the player's velocity by

        void UpdateGeometry(); // Updates the player's layer
        CoordinatePair pos;
        bool onGround = true;
        bool nextLevel = false;

    private:
        CoordinatePair velocity;
        float maxVel = 5;
        float size;

        bool CheckWin(CoordinatePair playerPos); // Checks if player is touching a win tile
        bool CheckDeath(CoordinatePair playerPos); // Checks if player is touching a death tile

        Layer *layer;
        Level *level;
        std::vector<ColorVertex> vertices;
        std::vector<uint16_t> indices;

        bool IsColliding(CoordinatePair playerPos); // Checks if player is colliding with a tile
        CoordinatePair CorrectCollision(CoordinatePair playerPos);

        // Outputs the coordinate offsets required to stop collision
    };
} // namespace Tina

#endif
