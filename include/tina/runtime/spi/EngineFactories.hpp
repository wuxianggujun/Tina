#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/task/TaskSystem.hpp>

#include <functional>
#include <memory>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#error "Tina Runtime requires C++23 std::move_only_function support"
#endif

namespace Tina {

using MonotonicClockFactory = std::move_only_function<
    Core::Result<std::unique_ptr<Core::IMonotonicClock>>()>;

struct EngineFactories final {
    MonotonicClockFactory createMonotonicClock;
    Platform::PlatformBackendFactory createPlatformBackend;
    Task::TaskSystemFactory createTaskSystem;
    Render::RenderDeviceFactory createRenderDevice;
};

} // namespace Tina
