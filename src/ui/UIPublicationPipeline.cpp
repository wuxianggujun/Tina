#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIPublicationPipeline.hpp>

namespace Tina::UI {

Core::Status UIPublicationPipeline::commitStructure()
{
    return m_context->m_impl->commitStructure();
}

UICommittedStructureView UIPublicationPipeline::committedStructure() const noexcept
{
    return m_context->m_impl->committedStructure();
}

Core::Status UIPublicationPipeline::commitLayout(UILogicalSize viewportSize)
{
    return m_context->m_impl->commitLayout(viewportSize);
}

UICommittedLayoutView UIPublicationPipeline::committedLayout() const noexcept
{
    return m_context->m_impl->committedLayout();
}

UILayoutDebugSnapshotView UIPublicationPipeline::committedLayoutDebugSnapshot() const noexcept
{
    return m_context->m_impl->committedLayoutDebugSnapshot();
}

UICommittedHitView UIPublicationPipeline::committedHit() const noexcept
{
    return m_context->m_impl->committedHit();
}

UICommittedPaintView UIPublicationPipeline::committedPaint() const noexcept
{
    return m_context->m_impl->committedPaint();
}

std::optional<UILogicalRect> UIPublicationPipeline::committedTextInputCaretRect() const noexcept
{
    return m_context->m_impl->committedTextInputCaretRectValue();
}

UICommittedSemanticsView UIPublicationPipeline::committedSemantics() const noexcept
{
    return m_context->m_impl->committedSemantics();
}

std::span<const u8> UIPublicationPipeline::glyphAtlasPixels() const noexcept
{
    return m_context->m_impl->glyphAtlasPixels();
}

u32 UIPublicationPipeline::glyphAtlasWidth() const noexcept
{
    return m_context->m_impl->glyphAtlasWidth();
}

u32 UIPublicationPipeline::glyphAtlasHeight() const noexcept
{
    return m_context->m_impl->glyphAtlasHeight();
}

u64 UIPublicationPipeline::glyphAtlasPageRevision() const noexcept
{
    return m_context->m_impl->glyphAtlasPageRevision();
}


} // namespace Tina::UI
