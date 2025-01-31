#include <iostream>
#include "tina/core/Core.hpp"
#include "tina/core/Engine.hpp"

int main()
{
    // Create engine instance
    Tina::Core::Engine engine;

    // Initialize engine
    if (!engine.initialize())
    {
        std::cerr << "Failed to initialize Tina engine!" << std::endl;
        return -1;
    }

    // Display engine version
    std::cout << "Tina Engine Version: " << engine.getVersion() << std::endl;

    // Use engine features
    std::cout << "Engine is running..." << std::endl;

    // Shutdown engine
    engine.shutdown();
    std::cout << "Engine shutdown complete." << std::endl;

    return 0;
}
