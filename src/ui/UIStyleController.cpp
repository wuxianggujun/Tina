#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIStyleController.hpp>

namespace Tina::UI {

const UITheme& UIStyleController::productTheme() const noexcept
{
    return m_context->m_impl->productTheme;
}

Core::Status UIStyleController::setProductTheme(const UITheme& theme)
{
    return m_context->m_impl->setProductTheme(theme);
}

Core::Result<UIStyleClassId> UIStyleController::registerStyleClass()
{
    return m_context->m_impl->registerStyleClass();
}

Core::Result<UIStyleTokenId>
UIStyleController::registerStyleColorToken(UIStraightSrgba8Color value)
{
    return m_context->m_impl->registerStyleColorToken(value);
}

Core::Result<UIStraightSrgba8Color>
UIStyleController::styleColorToken(UIStyleTokenId token) const
{
    return m_context->m_impl->styleColorToken(token);
}

Core::Status UIStyleController::setStyleColorToken(
    UIStyleTokenId token, UIStraightSrgba8Color value)
{
    return m_context->m_impl->setStyleColorToken(token, value);
}

Core::Status UIStyleController::installStyleSheet(
    std::span<const UIStyleBoxFillRule> rules)
{
    return m_context->m_impl->installStyleSheet(rules);
}


} // namespace Tina::UI
