#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/task/TaskSystem.hpp>

#include <functional>
#include <memory>
#include <variant>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#error "Tina Runtime requires C++23 std::move_only_function support"
#endif

namespace Tina {

using MonotonicClockFactory = std::move_only_function<Core::Result<std::unique_ptr<Core::IMonotonicClock>>()>;

struct IndependentPlatformRenderFactories final {
    Platform::PlatformBackendFactory createPlatformBackend;
    Render::RenderDeviceFactory createRenderDevice;
};

using WindowSurfaceRenderDeviceFactory = std::move_only_function<Core::Result<std::unique_ptr<Render::IRenderDevice>>(
    const Render::RenderDeviceCreateParams&, Integration::NativeWindowSurfaceLease)>;

struct WindowSurfacePlatformRenderFactories final {
    Integration::WindowSurfacePlatformBackendFactory createWindowSurfacePlatformBackend;
    WindowSurfaceRenderDeviceFactory createWindowSurfaceRenderDevice;
};

using PlatformRenderComposition =
    std::variant<IndependentPlatformRenderFactories, WindowSurfacePlatformRenderFactories>;

// One-shot composition input. The tagged Platform/Render branch prevents an
// invalid mixture of independent and native-window-aware factories.
struct EngineCompositionFactories final {
    MonotonicClockFactory createMonotonicClock;
    Task::TaskSystemFactory createTaskSystem;
    PlatformRenderComposition platformRender;
};

} // namespace Tina
