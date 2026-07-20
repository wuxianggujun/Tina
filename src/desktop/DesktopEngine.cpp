#include <tina/desktop/DesktopEngine.hpp>

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <exception>
#include <memory>
#include <new>
#include <utility>

namespace Tina::Desktop {
namespace {

[[nodiscard]] Core::Error createBootstrapBoundaryError(Core::ErrorCode errorCode)
{
    Core::Error error{errorCode, "An exception crossed the Desktop bootstrap boundary"};
    error.addContext("Desktop::CreateEngine");
    return error;
}

} // namespace

Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config) noexcept
{
    try
    {
        EngineCompositionFactories factories{
            .createMonotonicClock = []() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
                std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<Core::SteadyMonotonicClock>();
                return clock;
            },
            .createTaskSystem =
                [](const Task::TaskSystemCreateParams& params) {
                    // Production desktop uses bounded IO + Main completion (ADR 0017 first slice).
                    // Samples/tests may still inject DisabledTaskSystem explicitly.
                    Task::TaskSystemCreateParams effective = params;
                    if (effective.ioWorkerCount == 0)
                    {
                        effective.ioWorkerCount = 1;
                    }
                    if (effective.ioQueueCapacity == 0)
                    {
                        effective.ioQueueCapacity = 64;
                    }
                    if (effective.mainQueueCapacity == 0)
                    {
                        effective.mainQueueCapacity = 64;
                    }
                    return Task::createBoundedTaskSystem(effective);
                },
            .platformRender =
                WindowSurfacePlatformRenderFactories{
                    .createWindowSurfacePlatformBackend =
                        [](const Platform::PlatformBackendCreateParams& params) {
                            return Platform::createGlfwWindowSurfacePlatformBackend(params);
                        },
                    .createWindowSurfaceRenderDevice =
                        [](const Render::RenderDeviceCreateParams& params,
                           Integration::NativeWindowSurfaceLease lease) {
                            return Render::Bgfx::createBgfxRenderDevice(params, std::move(lease));
                        },
                },
        };

        return EngineHost::Create(config, std::move(factories));
    } catch (const std::bad_alloc&)
    {
        return Core::failure(createBootstrapBoundaryError(Core::CoreErrorCode::OutOfMemory));
    } catch (const std::exception&)
    {
        return Core::failure(createBootstrapBoundaryError(RuntimeErrorCode::EngineFactoryThrewException));
    } catch (...)
    {
        return Core::failure(createBootstrapBoundaryError(RuntimeErrorCode::EngineFactoryThrewException));
    }
}

} // namespace Tina::Desktop
