#include "tina/core/Core.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"

using namespace Tina;

int main()
{
    // Create engine instance
    Core::Engine engine;

    // Initialize engine
    if (!engine.initialize())
    {
        TINA_LOG_ERROR("main", "Engine initialization failed.");
        return -1;
    }

    if (!engine.run())
    {
        // Engine::run মেথড কল করুন, Context::run এর পরিবর্তে
        TINA_LOG_ERROR("main", "Application run failed.");
        engine.shutdown(); // শাটডাউন ইঞ্জিন
        return 1;
    }

    // Shutdown engine
    engine.shutdown();
    TINA_LOG_INFO("main", "Application finished successfully.");
    return 0;
}
