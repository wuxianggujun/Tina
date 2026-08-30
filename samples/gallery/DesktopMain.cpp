// Desktop front-end for the sample gallery.
//
// This file is the only thing the desktop build adds: the menu and the scenes live in
// tina_sample_gallery, which links no GLFW and no desktop bootstrap, so the Android JNI bridge links the
// same code. That split is the whole point -- eight existing samples cannot build for Android precisely
// because their content and their GLFW composition root share one translation unit.
//
// Unlike the gate samples, this one runs until the user closes the window. It is meant to be looked at,
// not to print evidence and return an exit code.

#include "GalleryActions.hpp"
#include "GalleryScene.hpp"

#include <tina/desktop/DesktopEngine.hpp>

#include <exception>
#include <iostream>
#include <new>

namespace {

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Sample Gallery";
    config.primaryWindow.title = "Tina Sample Gallery — pick a scene, Escape returns";
    config.primaryWindow.initialLogicalExtent = {900, 640};
    config.primaryWindow.initiallyVisible = true;
    Tina::Gallery::appendGalleryBindings(config);
    return config;
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_gallery_desktop\",\"code\":"
              << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

[[nodiscard]] int runGallery()
{
    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig());
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    auto application = Tina::Gallery::createGalleryApplication();
    if (application == nullptr)
    {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory,
                                     "The gallery application could not be created"});
        return 1;
    }
    auto runResult = (*hostResult)->run(*application);
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    try
    {
        return runGallery();
    } catch (const std::bad_alloc&)
    {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory,
                                     "The sample gallery ran out of memory"});
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the sample gallery boundary"};
        error.addContext("tina_sample_gallery_desktop",
                         exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        writeError(Tina::Core::Error{
            Tina::Core::CoreErrorCode::Internal,
            "A non-standard exception crossed the sample gallery boundary"});
        return 1;
    }
}
