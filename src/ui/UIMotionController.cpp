#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIMotionController.hpp>

namespace Tina::UI {

Core::Status UIMotionController::setMotionClock(const Core::IMonotonicClock* clock)
{
    return m_context->m_impl->setMotionClock(clock);
}

Core::Status UIMotionController::setReducedMotion(bool enabled)
{
    return m_context->m_impl->setReducedMotion(enabled);
}

bool UIMotionController::reducedMotion() const noexcept
{
    return m_context->m_impl->reducedMotion();
}

Core::Status UIMotionController::setStyleBackgroundColorTransition(const UITransitionSpec& spec)
{
    return m_context->m_impl->setStyleBackgroundColorTransition(spec);
}

UITransitionSpec UIMotionController::styleBackgroundColorTransition() const noexcept
{
    return m_context->m_impl->styleBackgroundColorTransition();
}

Core::Status UIMotionController::beginBackgroundColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginBackgroundColorTransition(node, target, spec);
}

Core::Status UIMotionController::beginBorderColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginBorderColorTransition(node, target, spec);
}

Core::Status UIMotionController::beginTextColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginTextColorTransition(node, target, spec);
}

Core::Status UIMotionController::beginOpacityTransition(
    UINodeId node, float targetOpacity, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginOpacityTransition(node, targetOpacity, spec);
}

Core::Status UIMotionController::beginCornerRadiusTransition(
    UINodeId node, float targetRadius, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginCornerRadiusTransition(node, targetRadius, spec);
}

Core::Status UIMotionController::beginVisualOffsetTransition(
    UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec)
{
    return m_context->m_impl->beginVisualOffsetTransition(node, targetOffsetX, targetOffsetY, spec);
}

Core::Result<UITimelineId> UIMotionController::createTimeline(const UITimelineDesc& desc)
{
    return m_context->m_impl->createTimeline(desc);
}

Core::Status UIMotionController::replaceTimeline(UITimelineId timeline, const UITimelineDesc& desc)
{
    return m_context->m_impl->replaceTimeline(timeline, desc);
}

Core::Status UIMotionController::playTimeline(UITimelineId timeline)
{
    return m_context->m_impl->playTimeline(timeline);
}

Core::Status UIMotionController::cancelTimeline(UITimelineId timeline)
{
    return m_context->m_impl->cancelTimeline(timeline);
}

Core::Status UIMotionController::destroyTimeline(UITimelineId timeline)
{
    return m_context->m_impl->destroyTimeline(timeline);
}

Core::Result<bool> UIMotionController::isTimelineActive(UITimelineId timeline) const
{
    return m_context->m_impl->isTimelineActive(timeline);
}

Core::Status UIMotionController::sampleMotion(Core::MonotonicTimePoint now)
{
    return m_context->m_impl->sampleMotion(now);
}


} // namespace Tina::UI
