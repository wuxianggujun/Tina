#include "UIScrollView.hpp"
#include "UIContext.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI {

UIScrollView::UIScrollView(const std::string& name)
    : UINode(name)
{
    setInteractable(true);
    setHoverable(true);
    setClipChildren(true);
    m_content = createChild<UINode>(name + ".Content");
    m_content->setInteractable(false);
    m_content->setSize(m_contentSize.x, m_contentSize.y);
}

void UIScrollView::setContentSize(float width, float height)
{
    m_contentSize.x = std::max(0.0f, width);
    m_contentSize.y = std::max(0.0f, height);
    if (m_content) m_content->setSize(m_contentSize.x, m_contentSize.y);
    clampOffsets();
}

void UIScrollView::setSmoothScroll(bool enabled) noexcept
{
    m_smoothScroll = enabled;
    if (!enabled) {
        m_scrollOffset = m_targetOffset;
        applyContentOffset();
    }
}

void UIScrollView::setScrollResponse(float responsePerSecond) noexcept
{
    m_scrollResponse = std::clamp(responsePerSecond, 1.0f, 60.0f);
}

void UIScrollView::setWheelStep(float logicalPixels) noexcept
{
    m_wheelStep = std::max(1.0f, logicalPixels);
}

Tina::Math::Vec2 UIScrollView::maximumScrollOffset() const noexcept
{
    const auto viewport = getSize();
    return {
        std::max(0.0f, m_contentSize.x - viewport.x),
        std::max(0.0f, m_contentSize.y - viewport.y)
    };
}

void UIScrollView::scrollTo(float x, float y)
{
    m_targetOffset = {x, y};
    clampOffsets();
    if (!m_smoothScroll) {
        m_scrollOffset = m_targetOffset;
        applyContentOffset();
    }
}

void UIScrollView::scrollBy(float deltaX, float deltaY)
{
    scrollTo(m_targetOffset.x + deltaX, m_targetOffset.y + deltaY);
}

void UIScrollView::onMouseWheel(float dx, float dy)
{
    float step = m_wheelStep;
    if (const auto* context = uiContext()) step = context->viewport().dp(step);

    const auto maxOffset = maximumScrollOffset();
    if (m_axis == UIScrollAxis::Horizontal ||
        (m_axis == UIScrollAxis::Both && maxOffset.y <= 0.0f)) {
        const float wheel = dx != 0.0f ? dx : dy;
        scrollBy(-wheel * step, 0.0f);
    } else {
        scrollBy(0.0f, -dy * step);
    }
}

void UIScrollView::onUpdate(float dt)
{
    clampOffsets();
    if (!m_smoothScroll) {
        m_scrollOffset = m_targetOffset;
        applyContentOffset();
        return;
    }

    const float safeDt = std::clamp(dt, 0.0f, 0.25f);
    const float alpha = 1.0f - std::exp(-m_scrollResponse * safeDt);
    m_scrollOffset.x += (m_targetOffset.x - m_scrollOffset.x) * alpha;
    m_scrollOffset.y += (m_targetOffset.y - m_scrollOffset.y) * alpha;
    if (std::abs(m_targetOffset.x - m_scrollOffset.x) < 0.05f) {
        m_scrollOffset.x = m_targetOffset.x;
    }
    if (std::abs(m_targetOffset.y - m_scrollOffset.y) < 0.05f) {
        m_scrollOffset.y = m_targetOffset.y;
    }
    applyContentOffset();
}

void UIScrollView::clampOffsets()
{
    const auto maximum = maximumScrollOffset();
    if (m_axis == UIScrollAxis::Vertical) {
        m_targetOffset.x = 0.0f;
        m_scrollOffset.x = 0.0f;
    } else if (m_axis == UIScrollAxis::Horizontal) {
        m_targetOffset.y = 0.0f;
        m_scrollOffset.y = 0.0f;
    }
    m_targetOffset.x = std::clamp(m_targetOffset.x, 0.0f, maximum.x);
    m_targetOffset.y = std::clamp(m_targetOffset.y, 0.0f, maximum.y);
    m_scrollOffset.x = std::clamp(m_scrollOffset.x, 0.0f, maximum.x);
    m_scrollOffset.y = std::clamp(m_scrollOffset.y, 0.0f, maximum.y);
    applyContentOffset();
}

void UIScrollView::applyContentOffset()
{
    if (m_content) m_content->setPosition(-m_scrollOffset.x, -m_scrollOffset.y);
}

} // namespace Tina::UI
