#include <tina/desktop/DesktopEngine.hpp>

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIContext.hpp>

#if defined(TINA_HAS_UI_FREETYPE)
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <exception>
#include <fstream>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Desktop {
namespace {

[[nodiscard]] Core::Error createBootstrapBoundaryError(Core::ErrorCode errorCode)
{
    Core::Error error{errorCode, "An exception crossed the Desktop bootstrap boundary"};
    error.addContext("Desktop::CreateEngine");
    return error;
}

#if defined(TINA_HAS_UI_FREETYPE)
[[nodiscard]] std::shared_ptr<std::vector<std::byte>> loadFontFixtureBytes(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::byte>>(size);
    if (size > 0)
    {
        input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    }
    if (!input)
    {
        return {};
    }
    return bytes;
}
#endif

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

#if defined(TINA_HAS_UI_FREETYPE)
#if defined(TINA_DESKTOP_UI_FONT_PATH)
        auto fontBytes = loadFontFixtureBytes(TINA_DESKTOP_UI_FONT_PATH);
#else
        std::shared_ptr<std::vector<std::byte>> fontBytes{};
#endif
        if (fontBytes && !fontBytes->empty())
        {
            factories.createPrimaryWindowUIContext =
                [fontBytes](
                    Platform::WindowId ownerWindow,
                    const UI::UIContextCapacityConfig& capacities,
                    std::pmr::memory_resource& resource) -> Core::Result<std::unique_ptr<UI::UIContext>> {
                    auto rasterizer = UI::createFreeTypeTextRasterizer({}, resource);
                    if (!rasterizer)
                    {
                        return Core::failure(std::move(rasterizer.error()));
                    }
                    auto context = UI::UIContext::Create(
                        ownerWindow, capacities, std::move(*rasterizer), resource);
                    if (!context)
                    {
                        return Core::failure(std::move(context.error()));
                    }
                    const auto open = (*context)->openTextFont(
                        std::span<const std::byte>(fontBytes->data(), fontBytes->size()));
                    if (!open)
                    {
                        return Core::failure(std::move(open.error()));
                    }
                    return std::move(*context);
                };
        }
#endif

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
