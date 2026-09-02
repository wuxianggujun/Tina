// Desktop composition root: Windows, Linux and macOS share this file.
//
// It is the only place allowed to name a platform type. Everything the game does lives in
// core/, so porting to another platform means adding a sibling of this file rather than
// touching content.

#include "GameApplication.hpp"

#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ContentRoot.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>

#include <exception>
#include <iostream>
#include <utility>

namespace {

// Answering "where is this product's content" is this file's other job, and the reason it
// belongs to a frontend rather than to core/. On desktop the answer is the directory holding
// the executable: it is the only anchor valid in the build tree and in an installed copy
// alike, and it is what makes the game runnable on a machine that never built it. A browser
// or an APK answers differently, which is exactly why content never asks this question.
[[nodiscard]] Tina::Core::Result<Tina::EngineConfig> createEngineConfig()
{
    auto applicationDirectory = Tina::Core::applicationDirectory();
    if (!applicationDirectory)
    {
        return Tina::Core::failure(std::move(applicationDirectory.error()));
    }
    auto contentRoot = Tina::Core::ContentRoot::Create(*applicationDirectory);
    if (!contentRoot)
    {
        return Tina::Core::failure(std::move(contentRoot.error()));
    }

    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "MyGame";
    config.contentRoot = std::move(*contentRoot);
    config.primaryWindow.title = "MyGame";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

[[nodiscard]] int run()
{
    auto config = createEngineConfig();
    if (!config)
    {
        writeError(config.error());
        return 1;
    }
    auto host = Tina::Desktop::CreateEngine(std::move(*config));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }
    auto application = MyGame::createApplication();
    if (application == nullptr)
    {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory,
                                     "the game application could not be created"});
        return 1;
    }
    auto exit = (*host)->run(*application);
    if (!exit)
    {
        writeError(exit.error());
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    // The engine reports failures as values, so an exception here means a std::
    // container gave up. Caught so the process still exits with a diagnosable code.
    try
    {
        return run();
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "an exception crossed the frontend boundary"};
        error.addContext("desktop", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::Internal,
                                     "a non-standard exception crossed the frontend boundary"});
        return 1;
    }
}
