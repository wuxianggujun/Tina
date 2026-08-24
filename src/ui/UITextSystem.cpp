#include "detail/UIContextImpl.hpp"

#include <tina/ui/UITextSystem.hpp>

namespace Tina::UI {

Core::Status UITextSystem::openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
{
    return m_context->m_impl->openTextFont(fontBytes, faceIndex);
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


} // namespace Tina::UI
