#include "detail/UIContextImpl.hpp"

#include <tina/ui/UITextSystem.hpp>

#include <new>
#include <stdexcept>

namespace Tina::UI {

Core::Result<UITextMetrics> UITextSystem::measureText(
    std::string_view utf8, const UITextStyle& style, UITextMeasureOptions options) const
{
    auto& impl = *m_context->m_impl;
    if (auto status = impl.ensureOwnerThread(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return impl.measureWidgetText(utf8, style, options);
}

Core::Status UITextSystem::openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
{
    return m_context->m_impl->openTextFont(fontBytes, faceIndex);
}

Core::Status UITextSystem::addFallbackFont(std::span<const std::byte> bytes, i32 faceIndex)
{
    auto& impl = *m_context->m_impl;
    if (auto status = impl.ensureOwnerThread(); !status) { return status; }
    if (impl.nodes.activeCount() != 0 || !impl.textRasterizer)
    {
        return Core::failure(UIErrorCode::InvalidFont, "Register fallback fonts before creating UI nodes");
    }
    auto face = impl.textRasterizer->openFace(bytes, faceIndex);
    if (!face) { return Core::failure(face.error()); }
    Core::Status status = Core::success();
    try { impl.textFallbackFaces.push_back(*face); }
    catch (const std::bad_alloc&)
    {
        auto closed = impl.textRasterizer->closeFace(*face);
        if (!closed) { return closed; }
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "UI fallback font list allocation failed");
    }
    catch (const std::length_error&)
    {
        auto closed = impl.textRasterizer->closeFace(*face);
        if (!closed) { return closed; }
        return Core::failure(UIErrorCode::CapacityExceeded, "UI fallback font list exceeds addressable storage");
    }
    status = impl.textRasterizer->setFallbackChain(impl.textFallbackFaces);
    if (!status)
    {
        impl.textFallbackFaces.pop_back();
        auto closed = impl.textRasterizer->closeFace(*face);
        if (!closed) { return closed; }
        return status;
    }
    return Core::success();
}

Core::Status UITextSystem::primeFontGlyphCache(std::span<const std::byte> cooked)
{
    auto& impl = *m_context->m_impl;
    if (auto status = impl.ensureOwnerThread(); !status) { return status; }
    if (impl.nodes.activeCount() != 0 || !impl.textRasterizer || !impl.textFace)
    {
        return Core::failure(UIErrorCode::InvalidFont, "Seed font glyphs before creating UI nodes");
    }
    return impl.textRasterizer->primeGlyphCache(impl.textFace, cooked);
}

Core::Status UITextSystem::setRasterScale(UITextRasterScale scale)
{
    auto& impl = *m_context->m_impl;
    if (auto status = impl.ensureOwnerThread(); !status) { return status; }
    if (auto status = validateUITextRasterScale(scale); !status) { return status; }
    if (impl.textRasterScale == scale) { return Core::success(); }
    for (u32 index = 0; index < impl.textStatesByIndex.size(); ++index)
    {
        if (!impl.textStatesByIndex[index].hasContent) { continue; }
        const auto node = impl.idForIndex(index);
        if (node && impl.contains(node))
        {
            if (auto status = impl.markPaintDirty(node); !status) { return status; }
        }
    }
    impl.textRasterScale = scale;
    return Core::success();
}

UINodeId UITextSystem::imeFocus() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->imeFocus();
}

bool UITextSystem::imeCompositionActive() const noexcept
{
    return m_context->m_impl->isOwnerThread() && m_context->m_impl->imeCompositionActive();
}

std::string_view UITextSystem::imePreeditUtf8() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return {};
    }
    return m_context->m_impl->imePreeditUtf8();
}

u32 UITextSystem::imePreeditCursorCodepoint() const noexcept
{
    if (!m_context->m_impl->isOwnerThread())
    {
        return 0;
    }
    return m_context->m_impl->imePreeditCursorCodepoint();
}

Core::Result<UITextInputRouteResult>
UITextSystem::routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
{
    return m_context->m_impl->routeTextComposition(window, platformFrame, sourceSequence, preeditUtf8, cursorCodepoint, stage);
}

Core::Result<UITextInputRouteResult> UITextSystem::routeTextInput(Platform::WindowId window,
                                                                          Platform::PlatformFrameId platformFrame,
                                                                          u64 sourceSequence,
                                                                          std::string_view committedUtf8)
{
    return m_context->m_impl->routeTextInput(window, platformFrame, sourceSequence, committedUtf8);
}

Core::Result<UITextInputRouteResult>
UITextSystem::routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                UITextEditCommand command, bool extendSelection)
{
    return m_context->m_impl->routeTextEditCommand(window, platformFrame, sourceSequence, command, extendSelection);
}

Core::Result<UITextClipboardRouteResult>
UITextSystem::routeTextClipboardCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame,
                                        u64 sourceSequence, UITextClipboardCommand command,
                                        Platform::IClipboard& clipboard)
{
    return m_context->m_impl->routeTextClipboardCommand(window, platformFrame, sourceSequence, command, clipboard);
}


} // namespace Tina::UI
