#include "PrimaryWindowUIDisplayCoordinator.hpp"

#include "PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>

#include <string_view>
#include <new>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

constexpr std::string_view BuildOperation = "PrimaryWindowUIDisplayCoordinator::buildForFrame";

[[nodiscard]] Core::Error lifecycleError(std::string_view message, std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext(BuildOperation, detail);
    return error;
}

[[nodiscard]] bool validPrimaryWindowIdentity(const Platform::WindowFrameSnapshot& primaryWindow) noexcept
{
    return primaryWindow.metrics.window.hasValue() && primaryWindow.metrics.revision != 0 &&
           primaryWindow.input.window == primaryWindow.metrics.window &&
           primaryWindow.input.sourceMetricsRevision == primaryWindow.metrics.revision;
}

[[nodiscard]] Render::UIPixelRect framebufferViewport(Platform::FramebufferExtent extent) noexcept
{
    return Render::UIPixelRect{
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height,
    };
}

[[nodiscard]] const Render::Texture2DFrameResourceResolver* findImageResolver(
    const void* userData, UI::UINodeId root) noexcept
{
    return userData == nullptr
               ? nullptr
               : static_cast<const PrimaryWindowUICapabilityState*>(userData)->findImageResolver(root);
}

} // namespace

Core::Result<PrimaryWindowUIDisplayCoordinator>
PrimaryWindowUIDisplayCoordinator::Create(Render::UIDisplayListCapacity capacity,
                                          std::pmr::memory_resource& storage)
{
    auto builder = Render::UIDisplayListBuilder::Create(capacity, storage);
    if (!builder)
    {
        return Core::failure(std::move(builder.error()));
    }
    try
    {
        std::pmr::vector<Integration::UIRenderImageResolutionCacheEntry> imageResolutionCache{&storage};
        imageResolutionCache.resize(capacity.commandCount);
        return PrimaryWindowUIDisplayCoordinator{
            std::move(*builder), std::move(imageResolutionCache)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Primary-window UI image cache allocation failed");
    }
}

PrimaryWindowUIDisplayCoordinator::PrimaryWindowUIDisplayCoordinator(
    Render::UIDisplayListBuilder builder,
    std::pmr::vector<Integration::UIRenderImageResolutionCacheEntry> imageResolutionCache) noexcept
    : builder_(std::move(builder)), imageResolutionCache_(std::move(imageResolutionCache)),
      ownerThreadId_(std::this_thread::get_id())
{
}

Core::Result<PrimaryWindowUIDisplayBuild> PrimaryWindowUIDisplayCoordinator::buildForFrame(
    UI::UIContext* context, const Platform::PlatformFrameView& platformFrame,
    const std::optional<Render::RenderSurfaceState>& primaryWindowSurface,
    const PrimaryWindowUICapabilityState& capabilityState,
    Render::FrameResourceSink& resourceSink)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUIDisplayCoordinator may build only from its owner thread");
    }

    const Platform::PlatformFrameId frameId = platformFrame.id();
    if (!frameId.hasValue())
    {
        return failAttempt(lifecycleError("A UI DisplayList build requires a valid PlatformFrameId"));
    }
    if (lastAttemptedFrame_.hasValue() && frameId <= lastAttemptedFrame_)
    {
        return failAttempt(lifecycleError("A UI DisplayList build requires a strictly newer PlatformFrameId"));
    }
    lastAttemptedFrame_ = frameId;

    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (primaryWindow == nullptr && context == nullptr)
    {
        if (primaryWindowSurface.has_value())
        {
            return failAttempt(lifecycleError(
                "A headless Platform frame must not carry a primary-window render surface"));
        }

        auto build = Integration::buildUIDisplayList(builder_, UI::UICommittedPaintView{}, {}, {
            .resourceSink = &resourceSink,
            .resolverLookup = {.userData = &capabilityState, .find = &findImageResolver},
            .cache = imageResolutionCache_,
        });
        if (!build)
        {
            auto error = std::move(build.error());
            error.addContext(BuildOperation, "headless empty DisplayList publication");
            return Core::failure(std::move(error));
        }
        return PrimaryWindowUIDisplayBuild{
            .displayList = build->displayList,
            .conversionStatistics = build->statistics,
        };
    }
    if (primaryWindow == nullptr || context == nullptr)
    {
        return failAttempt(lifecycleError(
            "The primary window and Runtime UI context must either both exist or both be absent"));
    }
    if (!validPrimaryWindowIdentity(*primaryWindow))
    {
        return failAttempt(lifecycleError(
            "The primary Platform snapshot has an invalid or inconsistent identity/revision"));
    }
    if (context->ownerWindow() != primaryWindow->metrics.window)
    {
        return failAttempt(lifecycleError(
            "The Runtime UI context does not belong to the primary window"));
    }
    if (lastMetricsRevision_ != 0 && primaryWindow->metrics.revision < lastMetricsRevision_)
    {
        return failAttempt(lifecycleError(
            "The primary-window metrics revision moved backward"));
    }
    lastMetricsRevision_ = primaryWindow->metrics.revision;

    const UI::UICommittedPaintView paintView = context->publication().committedPaint();
    const UI::UILogicalSize expectedLogicalViewport{
        .width = static_cast<float>(primaryWindow->metrics.logicalExtent.width),
        .height = static_cast<float>(primaryWindow->metrics.logicalExtent.height),
    };
    if (paintView.viewportSize() != expectedLogicalViewport)
    {
        return failAttempt(lifecycleError(
            "The committed UI paint viewport does not match the primary-window logical extent"));
    }

    Render::UIPixelRect viewport = framebufferViewport(primaryWindow->metrics.framebufferExtent);
    if (primaryWindowSurface.has_value())
    {
        const Render::RenderSurfaceState& surface = *primaryWindowSurface;
        if (!surface.surface.hasValue() || surface.surfaceRevision == 0 || surface.sourceMetricsRevision == 0)
        {
            return failAttempt(lifecycleError(
                "The primary-window render surface has an invalid identity or revision"));
        }
        if (surface.sourceMetricsRevision != primaryWindow->metrics.revision)
        {
            return failAttempt(lifecycleError(
                "The primary-window render surface does not use the current metrics revision"));
        }
        if (surface.framebufferExtent.width != primaryWindow->metrics.framebufferExtent.width ||
            surface.framebufferExtent.height != primaryWindow->metrics.framebufferExtent.height)
        {
            return failAttempt(lifecycleError(
                "The primary-window render surface and Platform framebuffer extents disagree"));
        }

        switch (surface.availability)
        {
        case Render::RenderSurfaceAvailability::Active:
            if (surface.framebufferExtent.width == 0 || surface.framebufferExtent.height == 0)
            {
                return failAttempt(lifecycleError(
                    "An active primary-window render surface must have a non-zero framebuffer extent"));
            }
            break;
        case Render::RenderSurfaceAvailability::Suspended:
            viewport = {};
            break;
        default:
            return failAttempt(lifecycleError(
                "The primary-window render surface has an invalid availability value"));
        }
    }

    auto build = Integration::buildUIDisplayList(
        builder_, paintView, Integration::UIRenderViewportMapping{.framebufferViewport = viewport}, {
            .resourceSink = &resourceSink,
            .resolverLookup = {.userData = &capabilityState, .find = &findImageResolver},
            .cache = imageResolutionCache_,
        });
    if (!build)
    {
        auto error = std::move(build.error());
        error.addContext(BuildOperation, "UI paint to Render DisplayList conversion");
        return Core::failure(std::move(error));
    }
    return PrimaryWindowUIDisplayBuild{
        .displayList = build->displayList,
        .conversionStatistics = build->statistics,
    };
}

Render::UIDisplayListView PrimaryWindowUIDisplayCoordinator::publishedView() const noexcept
{
    return builder_.publishedView();
}

Render::UIDisplayListBuilderStatistics PrimaryWindowUIDisplayCoordinator::builderStatistics() const noexcept
{
    return builder_.statistics();
}

Core::Result<PrimaryWindowUIDisplayBuild>
PrimaryWindowUIDisplayCoordinator::failAttempt(Core::Error error)
{
    invalidatePublishedView();
    return Core::failure(std::move(error));
}

void PrimaryWindowUIDisplayCoordinator::invalidatePublishedView() noexcept
{
    Core::Status beginStatus = builder_.beginFrame();
    if (beginStatus)
    {
        builder_.rollback();
    }
}

} // namespace Tina::Runtime::Detail
