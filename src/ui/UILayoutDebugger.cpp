#include "detail/UIContextImpl.hpp"

#include <tina/ui/UILayoutDebugger.hpp>

namespace Tina::UI {

UILayoutDebugSnapshotView UILayoutDebugger::committedSnapshot() const noexcept
{
    return m_context != nullptr ? m_context->m_impl->committedLayoutDebugSnapshot()
                                : UILayoutDebugSnapshotView{};
}

UILayoutDebugOptions UILayoutDebugger::options() const noexcept
{
    return m_context != nullptr ? m_context->m_impl->layoutDebugOptionsValue()
                                : UILayoutDebugOptions{};
}

Core::Status UILayoutDebugger::setOptions(UILayoutDebugOptions options)
{
    return m_context != nullptr
               ? m_context->m_impl->setLayoutDebugOptions(options)
               : fail(UIErrorCode::WrongContext, "UI layout debugger is not bound to a context");
}

} // namespace Tina::UI
