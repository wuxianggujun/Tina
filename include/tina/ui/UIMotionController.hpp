#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

namespace Tina::UI {

class UIContext;

class UIMotionController final {
  public:
    [[nodiscard]] Core::Status setMotionClock(const Core::IMonotonicClock* clock);
    [[nodiscard]] Core::Status setReducedMotion(bool enabled);
    [[nodiscard]] bool reducedMotion() const noexcept;
    [[nodiscard]] Core::Status setStyleBackgroundColorTransition(const UITransitionSpec& spec);
    [[nodiscard]] UITransitionSpec styleBackgroundColorTransition() const noexcept;
    [[nodiscard]] Core::Status beginBackgroundColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginBorderColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginTextColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginOpacityTransition(
        UINodeId node, float targetOpacity, const UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginCornerRadiusTransition(
        UINodeId node, float targetRadius, const UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginVisualOffsetTransition(
        UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec);
    [[nodiscard]] Core::Result<UITimelineId> createTimeline(const UITimelineDesc& desc);
    [[nodiscard]] Core::Status replaceTimeline(UITimelineId timeline,
                                               const UITimelineDesc& desc);
    [[nodiscard]] Core::Status playTimeline(UITimelineId timeline);
    [[nodiscard]] Core::Status cancelTimeline(UITimelineId timeline);
    [[nodiscard]] Core::Status destroyTimeline(UITimelineId timeline);
    [[nodiscard]] Core::Result<bool> isTimelineActive(UITimelineId timeline) const;
    [[nodiscard]] Core::Status sampleMotion(Core::MonotonicTimePoint now);

  private:
    friend class UIContext;

    explicit UIMotionController(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
