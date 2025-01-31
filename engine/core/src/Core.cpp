#include "tina/core/Core.hpp"

namespace Tina {
namespace Core {

Engine::Engine()
{
}

Engine::~Engine()
{
    shutdown();
}

bool Engine::initialize()
{
    // Add initialization code here
    return true;
}

void Engine::shutdown()
{
    // Add cleanup code here
}

const char* Engine::getVersion() const
{
    return TINA_VERSION;
}

} // namespace Core
} // namespace Tina
