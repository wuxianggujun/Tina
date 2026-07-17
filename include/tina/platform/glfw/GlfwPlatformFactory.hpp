#pragma once

#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>

namespace Tina::Platform {

// Creates Tina's production desktop window/input adapter. Creation, polling,
// shutdown, and destruction must all occur on the process main/platform
// thread, as required by GLFW. Tina exclusively owns the process-wide GLFW
// initialization lifetime while this backend exists; embedding it into a host
// that separately initializes GLFW is unsupported. GLFW remains an
// implementation detail of the returned backend and never crosses this API.
[[nodiscard]] Core::Result<std::unique_ptr<IPlatformBackend>>
createGlfwPlatformBackend(const PlatformBackendCreateParams& params);

// Creates the deferred-publication variant used by the desktop
// WindowSurface+Render composition. The window remains hidden until
// IWindowSurfacePlatformBackend::publishPrimaryWindow succeeds.
[[nodiscard]] Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createGlfwWindowSurfacePlatformBackend(const PlatformBackendCreateParams& params);

} // namespace Tina::Platform
