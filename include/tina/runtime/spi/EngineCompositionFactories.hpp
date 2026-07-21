#pragma once

#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/Window.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/task/TaskSystem.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIContextConfig.hpp>

#include <functional>
#include <memory>
#include <memory_resource>
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

// Optional override for primary-window UIContext construction. Default is
// UIContext::Create(window, capacities, resource) with the placeholder
// rasterizer. Desktop FreeType samples may inject a FreeType rasterizer and
// open a fixture face here. Must not retain Platform views.
using PrimaryWindowUIContextFactory = std::move_only_function<Core::Result<std::unique_ptr<UI::UIContext>>(
    Platform::WindowId ownerWindow,
    const UI::UIContextCapacityConfig& capacities,
    std::pmr::memory_resource& resource)>;

// Optional AudioEngine construction (M11-A14). Empty → no Audio module.
// Prefer AudioEngine::Create (Disabled) for Null graphs; miniaudio device stays
// sample/adapter private and is not required by Runtime.
using AudioEngineFactory = std::move_only_function<Core::Result<Audio::AudioEngine>()>;

// One-shot composition input. The tagged Platform/Render branch prevents an
// invalid mixture of independent and native-window-aware factories.
struct EngineCompositionFactories final {
    MonotonicClockFactory createMonotonicClock;
    Task::TaskSystemFactory createTaskSystem;
    PlatformRenderComposition platformRender;
    PrimaryWindowUIContextFactory createPrimaryWindowUIContext{};
    AudioEngineFactory createAudioEngine{};
};

} // namespace Tina
