#pragma once

#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>

#include <string>

namespace Tina::Platform {

struct Html5PlatformCreateParams final {
    PlatformBackendCreateParams base{};
    // CSS selector of the canvas the backend binds to. Emscripten's shell names its
    // canvas "canvas", so that is the default; a host embedding Tina in a larger page
    // passes its own selector.
    std::string canvasSelector = "#canvas";
    // Drives the canvas backing store from the CSS size times devicePixelRatio on
    // every resize. Off leaves the backing store alone, which is what a host that
    // sizes the canvas itself needs.
    bool trackElementSize = true;
};

// Creates the HTML5 backend bound to a browser canvas.
//
// Must be called on the thread that runs the browser event loop, and that thread
// stays the owner thread for every later poll. The backend registers Emscripten
// event callbacks at creation and unregisters them in shutdown().
//
// This backend never blocks. It cannot: a browser tab that blocks its main thread
// stops delivering the very events the backend is waiting for. pollFrame() drains
// whatever the callbacks have queued since the previous poll and returns, so the
// caller must drive it from emscripten_set_main_loop rather than a while loop.
// The canvas is the WindowSurface, so the backend is surface-aware: a render device binds
// to it through a lease rather than being created independently.
[[nodiscard]] Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createHtml5WindowSurfacePlatformBackend(const Html5PlatformCreateParams& params);

// Same backend without the surface seam, for a Null render device that draws nowhere.
[[nodiscard]] Core::Result<std::unique_ptr<IPlatformBackend>> createHtml5PlatformBackend(
    const Html5PlatformCreateParams& params);

} // namespace Tina::Platform
