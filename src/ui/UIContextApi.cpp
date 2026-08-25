#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIInputRouter.hpp>
#include <tina/ui/UILayoutDebugger.hpp>
#include <tina/ui/UIMotionController.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UIStyleController.hpp>
#include <tina/ui/UITextSystem.hpp>

namespace Tina::UI {

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::pmr::memory_resource& resource)
{
    // Validate before any allocation against the caller's resource so failed
    // Create remains allocation-free for invalid window/capacity probes.
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (Core::Status status = validateUIContextCapacityConfig(capacityConfig); !status)
    {
        return Core::failure(status.error());
    }

    // Placeholder rasterizer construction allocates against the caller's PMR and
    // must surface OOM as Result, not an uncaught bad_alloc (M10-A39 gate).
    try
    {
        auto rasterizer = createPlaceholderTextRasterizer({}, resource);
        if (!rasterizer)
        {
            return Core::failure(rasterizer.error());
        }
        return Create(ownerWindow, capacityConfig, std::move(*rasterizer), resource);
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::unique_ptr<IUITextRasterizer> textRasterizer,
                                                           std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (!textRasterizer)
    {
        return fail(UIErrorCode::InvalidFont, "UI context text rasterizer is null");
    }

    auto normalizedResult = Detail::normalizeUIContextCapacityConfig(capacityConfig);
    if (!normalizedResult)
    {
        return Core::failure(normalizedResult.error());
    }

    try
    {
        const std::thread::id ownerThreadId = std::this_thread::get_id();
        auto lifetime = std::make_shared<Detail::UIContextLifetimeControl>(
            ownerThreadId, normalizedResult->rootCapacity, normalizedResult->routedPointerListenerCapacity);
        auto implResult = Impl::Create(ownerWindow, *normalizedResult, lifetime, resource);
        if (!implResult)
        {
            return Core::failure(implResult.error());
        }

        // Best-effort open of the built-in/empty face. Placeholder accepts {}.
        // FreeType adapters reject empty bytes; they remain without a face until
        // a later font-binding slice opens real bytes.
        auto faceResult = textRasterizer->openFace({});
        if (faceResult)
        {
            (*implResult)->textFace = *faceResult;
        }
        (*implResult)->textRasterizer = std::move(textRasterizer);

        const usize allocationBeforeGlyphAtlas =
            (*implResult)->allocationLedger->statistics().currentBytes;
        auto atlasResult = UIGlyphAtlas::Create(
            UIGlyphAtlasCapacity{
                .width = 512,
                .height = 512,
                .maxGlyphs = 1024,
            },
            (*implResult)->allocationMemoryResource());
        if (atlasResult)
        {
            (*implResult)->glyphAtlas = std::move(*atlasResult);
        }
        const usize allocationAfterGlyphAtlas =
            (*implResult)->allocationLedger->statistics().currentBytes;
        (*implResult)->pmrGlyphAtlasBytes =
            allocationIncrease(allocationBeforeGlyphAtlas, allocationAfterGlyphAtlas);

        auto context = std::unique_ptr<UIContext>(new UIContext(std::move(*implResult)));
        lifetime->attach(*context);
        return context;
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

UIContext::UIContext(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl))
{
}

UIContext::~UIContext() noexcept
{
    if (m_impl)
    {
        if (!m_impl->isOwnerThread() || m_impl->routeDispatchDepth != 0 ||
            m_impl->routedPointerListenerRegistry.operationInProgress() ||
            m_impl->buttonActionRegistry.operationInProgress() ||
            m_impl->sliderChangeCallbackRegistry.operationInProgress())
        {
            std::terminate();
        }
        m_impl->detachLifetime(this);
    }
}

Platform::WindowId UIContext::ownerWindow() const noexcept
{
    return m_impl->ownerWindow;
}

bool UIContext::contains(UINodeId node) const noexcept
{
    return m_impl->isOwnerThread() && m_impl->contains(node);
}


UIAuthoring UIContext::authoring() noexcept
{
    return UIAuthoring(*this);
}

UIStyleController UIContext::style() noexcept
{
    return UIStyleController(*this);
}

UIMotionController UIContext::motion() noexcept
{
    return UIMotionController(*this);
}

UITextSystem UIContext::text() noexcept
{
    return UITextSystem(*this);
}

UIPublicationPipeline UIContext::publication() noexcept
{
    return UIPublicationPipeline(*this);
}

UILayoutDebugger UIContext::layoutDebugger() noexcept
{
    return UILayoutDebugger(*this);
}

UIInputRouter UIContext::input() noexcept
{
    return UIInputRouter(*this);
}
UIContextStatistics UIContext::statistics() const noexcept
{
    return m_impl->statistics();
}

usize UIContext::liveNodeCount() const noexcept
{
    return m_impl->nodes.activeCount();
}

usize UIContext::liveRootCount() const noexcept
{
    return m_impl->liveRootCount;
}


} // namespace Tina::UI
