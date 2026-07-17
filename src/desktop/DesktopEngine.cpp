#include <tina/desktop/DesktopEngine.hpp>

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

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
                return std::move(clock);
            },
            .createTaskSystem =
                [](const Task::TaskSystemCreateParams& params) { return Task::createDisabledTaskSystem(params); },
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
