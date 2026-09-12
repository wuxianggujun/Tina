#include <tina/android/AndroidEngine.hpp>

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITextSystem.hpp>

#if defined(TINA_HAS_UI_FREETYPE)
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <exception>
#include <new>
#include <span>
#include <thread>
#include <utility>

namespace Tina::Android {

Core::Result<EngineInstance> CreateEngine(const EngineConfig& config,
                                         CreateEngineOptions options) noexcept
{
    try
    {
        // Factories may outlive this call. A shared capture avoids retaining a
        // reference to a stack local solely to obtain the borrowed lifecycle facet.
        auto platformView = std::make_shared<Platform::IAndroidPlatformBackend*>(nullptr);
        EngineCompositionFactories factories{
            .createMonotonicClock = []() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
                return std::unique_ptr<Core::IMonotonicClock>{std::make_unique<Core::SteadyMonotonicClock>()};
            },
            .createTaskSystem = [](const Task::TaskSystemCreateParams& params) {
                const unsigned cores = std::thread::hardware_concurrency();
                const auto effective = Task::resolveDesktopTaskSystemParams(
                    params, cores == 0U ? 1U : static_cast<u32>(cores));
                return Task::createBoundedTaskSystem(effective);
            },
            .platformRender = WindowSurfacePlatformRenderFactories{
                .createWindowSurfacePlatformBackend =
                    [window = std::move(options.window), platformView](
                        const Platform::PlatformBackendCreateParams& params) mutable
                        -> Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>> {
                        window.platform = params;
                        auto backend = Platform::createAndroidWindowSurfacePlatformBackend(window);
                        if (!backend) { return backend; }
                        *platformView = dynamic_cast<Platform::IAndroidPlatformBackend*>(backend->get());
                        if (*platformView == nullptr)
                        {
                            return Core::failure(Core::CoreErrorCode::Internal,
                                                 "Android backend has no lifecycle facet");
                        }
                        return backend;
                    },
                .createWindowSurfaceRenderDevice =
                    [rendererApi = options.rendererApi](const Render::RenderDeviceCreateParams& params,
                                                       Integration::NativeWindowSurfaceLease lease) {
                        auto effective = params;
                        effective.rendererApi = rendererApi;
                        return Render::Bgfx::createBgfxRenderDevice(effective, std::move(lease));
                    },
            },
        };

#if defined(TINA_HAS_UI_FREETYPE)
        if (options.uiFontBytes && !options.uiFontBytes->empty())
        {
            factories.createPrimaryWindowUIContext =
                [font = std::move(options.uiFontBytes), atlas = std::move(options.uiFontAtlasBytes),
                 fallbacks = std::move(options.uiFallbackFontBytes)](
                    Platform::WindowId window, const UI::UIContextCapacityConfig& capacities,
                    std::pmr::memory_resource& resource) -> Core::Result<std::unique_ptr<UI::UIContext>> {
                    auto rasterizer = UI::createFreeTypeTextRasterizer({}, resource);
                    if (!rasterizer) { return Core::failure(std::move(rasterizer.error())); }
                    auto context = UI::UIContext::Create(window, capacities, std::move(*rasterizer), resource);
                    if (!context) { return Core::failure(std::move(context.error())); }
                    if (auto status = (*context)->text().openTextFont(std::span<const std::byte>(*font)); !status)
                    { return Core::failure(std::move(status.error())); }
                    for (const auto& fallback : fallbacks)
                    {
                        if (!fallback || fallback->empty())
                        { return Core::failure(Core::CoreErrorCode::InvalidArgument, "UI fallback font bytes are empty"); }
                        if (auto status = (*context)->text().addFallbackFont(*fallback); !status)
                        { return Core::failure(std::move(status.error())); }
                    }
                    if (atlas)
                    {
                        if (auto status = (*context)->text().primeFontGlyphCache(*atlas); !status)
                        { return Core::failure(std::move(status.error())); }
                    }
                    return std::move(*context);
                };
        }
#else
        if (options.uiFontBytes || options.uiFontAtlasBytes || !options.uiFallbackFontBytes.empty())
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Android UI font bytes require an SDK with UIFreetype capability");
        }
#endif
        auto host = EngineHost::Create(config, std::move(factories));
        if (!host) { return Core::failure(std::move(host.error())); }
        return EngineInstance{std::move(*host), *platformView};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Android::CreateEngine allocation failed");
    } catch (const std::exception&)
    {
        return Core::failure(RuntimeErrorCode::EngineFactoryThrewException,
                             "An exception crossed Android::CreateEngine");
    } catch (...)
    {
        return Core::failure(RuntimeErrorCode::EngineFactoryThrewException,
                             "A non-standard exception crossed Android::CreateEngine");
    }
}

} // namespace Tina::Android
