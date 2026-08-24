#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Status UIContext::Impl::setMotionClock(const Core::IMonotonicClock* clock)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    motionClock = clock != nullptr ? clock : &motionDefaultClock;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setReducedMotion(bool enabled)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (reducedMotionEnabled == enabled)
    {
        return Core::success();
    }
    if (!enabled)
    {
        reducedMotionEnabled = false;
        return Core::success();
    }
    if (Core::Status collected = collectActiveTimelineDirtyNodes(); !collected)
    {
        return collected;
    }
    if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
    {
        return dirty;
    }
    reducedMotionEnabled = true;
    // Snap every active track to target and free slots (ADR: reduced-motion
    // does not keep tracks on the active list after the call).
    motionTrackStorage.snapAllActive();
    for (const Detail::UIMotionTrackStorage::Completed& completed :
         motionTrackStorage.lastCompleted())
    {
        if (!contains(completed.node))
        {
            continue;
        }
        applyCompletedMotion(completed);
    }
    timelineStorage.snapAllActive();
    const auto timelineTargets = timelineStorage.lastTargets();
    for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
    {
        applyTimelineTarget(target);
    }
    if (!motionTrackStorage.lastCompleted().empty() || !timelineTargets.empty())
    {
        phaseDirty |= PhasePaint;
    }
    return Core::success();
}

[[nodiscard]] bool UIContext::Impl::reducedMotion() const noexcept
{
    return reducedMotionEnabled;
}

[[nodiscard]] Core::Status UIContext::Impl::setStyleBackgroundColorTransition(const UITransitionSpec& spec)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (spec.property != UIAnimatableProperty::BackgroundColor)
    {
        return fail(UIErrorCode::InvalidStyle, "Style background transition only supports BackgroundColor");
    }
    if (!isValidUIEasing(spec.easing))
    {
        return fail(UIErrorCode::InvalidStyle, "Style background transition easing is invalid");
    }
    if (spec.duration.count() < 0.0 || spec.delay.count() < 0.0 || !std::isfinite(spec.duration.count()) ||
        !std::isfinite(spec.delay.count()))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Style background transition duration/delay must be finite and non-negative");
    }
    const bool wasEnabled = styleBackgroundTransitionEnabled();
    const bool willEnable = spec.duration.count() > 0.0;
    if (!wasEnabled && willEnable)
    {
        usize requiredSlots = 0;
        for (u32 index = 0; index < idsByIndex.size(); ++index)
        {
            const UINodeId node = idForIndex(index);
            if (!contains(node) ||
                !styleSheetStorage.hasStatefulBoxFillCandidateValidated(styleRolesByNodeIndex[index],
                                                                        styleClassesFor(index)) ||
                motionTrackStorage.hasTrack(node, UIAnimatableProperty::BackgroundColor))
            {
                continue;
            }
            ++requiredSlots;
        }
        if (requiredSlots > motionTrackStorage.availableCount())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI style background transitions exceed the reserved motion track capacity");
        }
        for (u32 index = 0; index < idsByIndex.size(); ++index)
        {
            const UINodeId node = idForIndex(index);
            if (!contains(node) || !styleSheetStorage.hasStatefulBoxFillCandidateValidated(
                                       styleRolesByNodeIndex[index], styleClassesFor(index)))
            {
                continue;
            }
            if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::BackgroundColor))
            {
                return fail(UIErrorCode::InvalidStyle,
                            "Style motion reservation conflicts with an active keyframe timeline");
            }
            if (Core::Status reserved =
                    motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
                !reserved)
            {
                std::terminate();
            }
        }
    } else if (wasEnabled && !willEnable)
    {
        motionTrackStorage.releaseAllPersistentReservations(UIAnimatableProperty::BackgroundColor);
    }

    styleBackgroundColorTransitionSpec = spec;
    return Core::success();
}

[[nodiscard]] UITransitionSpec UIContext::Impl::styleBackgroundColorTransition() const noexcept
{
    return styleBackgroundColorTransitionSpec;
}

void UIContext::Impl::commitMotionProperty(const Detail::UIMotionTrackStorage::Completed& completed) noexcept
{
    const u32 index = completed.node.index();
    switch (completed.property)
    {
    case UIAnimatableProperty::BackgroundColor:
        boxPaintsByIndex[index].solidFill = UISolidFill{.color = completed.color};
        detachThemeBinding(index, ThemeBindingBoxPaint);
        resolvedBoxFillCacheByNodeIndex[index] = premultiply(completed.color);
        setResolvedStyleColorTokenDependency(index, {});
        break;
    case UIAnimatableProperty::BorderColor:
        boxPaintsByIndex[index].borderLight = completed.color;
        boxPaintsByIndex[index].borderDark = completed.color;
        detachThemeBinding(index, ThemeBindingBoxPaint);
        break;
    case UIAnimatableProperty::TextColor:
        if (index < textStatesByIndex.size() && textStatesByIndex[index].hasContent)
        {
            textStatesByIndex[index].style.color = completed.color;
            detachThemeBinding(index, ThemeBindingTextStyle);
            localTextColorCacheByIndex[index] = premultiply(completed.color);
        }
        break;
    case UIAnimatableProperty::Opacity:
        if (index < presentationOpacityValidByNodeIndex.size())
        {
            presentationOpacityByNodeIndex[index] =
                std::clamp(completed.scalar, 0.0F, 1.0F);
            presentationOpacityValidByNodeIndex[index] = 1;
        }
        break;
    case UIAnimatableProperty::CornerRadius:
        boxPaintsByIndex[index].cornerRadii = UILogicalCornerRadii::uniform(
            (std::max)(0.0F, completed.scalar));
        detachThemeBinding(index, ThemeBindingBoxPaint);
        break;
    case UIAnimatableProperty::VisualOffset:
        if (index < presentationOffsetValidByNodeIndex.size())
        {
            presentationOffsetXByNodeIndex[index] = completed.offset.x;
            presentationOffsetYByNodeIndex[index] = completed.offset.y;
            presentationOffsetValidByNodeIndex[index] = 1;
        }
        break;
    case UIAnimatableProperty::LayoutWidth:
    case UIAnimatableProperty::LayoutHeight:
    case UIAnimatableProperty::LayoutOffset:
        // Direct transition storage never creates layout tracks.
        break;
    }
}

void UIContext::Impl::applyCompletedMotion(const Detail::UIMotionTrackStorage::Completed& completed) noexcept
{
    if (!contains(completed.node))
    {
        return;
    }
    if (completed.completionMode == Detail::UIMotionTrackStorage::CompletionMode::StylePresentationOnly)
    {
        return;
    }

    commitMotionProperty(completed);
}

void UIContext::Impl::applyTimelineTarget(const Detail::UIKeyframeTimelineStorage::Target& target) noexcept
{
    if (!contains(target.node))
    {
        return;
    }
    UILayoutStyle& layout = layoutStylesByIndex[target.node.index()];
    switch (target.property)
    {
    case UIAnimatableProperty::LayoutWidth:
        layout.size.width = UILayoutLength::Px((std::max)(0.0F, target.value.scalar));
        return;
    case UIAnimatableProperty::LayoutHeight:
        layout.size.height = UILayoutLength::Px((std::max)(0.0F, target.value.scalar));
        return;
    case UIAnimatableProperty::LayoutOffset:
        layout.overlay.offset.x = UILayoutLength::Px(target.value.offset.x);
        layout.overlay.offset.y = UILayoutLength::Px(target.value.offset.y);
        return;
    case UIAnimatableProperty::BackgroundColor:
    case UIAnimatableProperty::BorderColor:
    case UIAnimatableProperty::TextColor:
    case UIAnimatableProperty::Opacity:
    case UIAnimatableProperty::CornerRadius:
    case UIAnimatableProperty::VisualOffset:
        break;
    }
    Detail::UIMotionTrackStorage::Completed completed{
        .node = target.node,
        .property = target.property,
        .completionMode = Detail::UIMotionTrackStorage::CompletionMode::CommitProperty,
    };
    switch (target.value.kind)
    {
    case UIKeyframeValueKind::Color:
        completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Color;
        completed.color = target.value.color;
        break;
    case UIKeyframeValueKind::Scalar:
        completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar;
        completed.scalar = target.value.scalar;
        break;
    case UIKeyframeValueKind::Offset:
        completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Offset;
        completed.offset = {
            .x = target.value.offset.x,
            .y = target.value.offset.y,
        };
        break;
    }
    // Timeline control operations preflight and mark the complete target
    // set before mutating playback. Candidate completion already built the
    // matching paint snapshot, so target application itself stays infallible.
    commitMotionProperty(completed);
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::unpremultiplyColor(UIPremultipliedRgba8Color premul) const noexcept
{
    if (premul.alpha == 0)
    {
        return {};
    }
    const float inv = 255.0F / static_cast<float>(premul.alpha);
    return UIStraightSrgba8Color{
        .red = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.red) * inv + 0.5F)),
        .green = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.green) * inv + 0.5F)),
        .blue = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.blue) * inv + 0.5F)),
        .alpha = premul.alpha,
    };
}

[[nodiscard]] Detail::UIMotionTrackStorage::NodePresentation
UIContext::Impl::motionPresentationFor(UINodeId node) const noexcept
{
    auto presentation = motionTrackStorage.presentationFor(node);
    const auto timeline = timelineStorage.presentationFor(node);
    if (timeline.hasBackgroundColor)
    {
        presentation.hasBackgroundColor = true;
        presentation.backgroundColor = timeline.backgroundColor;
    }
    if (timeline.hasBorderColor)
    {
        presentation.hasBorderColor = true;
        presentation.borderColor = timeline.borderColor;
    }
    if (timeline.hasTextColor)
    {
        presentation.hasTextColor = true;
        presentation.textColor = timeline.textColor;
    }
    if (timeline.hasOpacity)
    {
        presentation.hasOpacity = true;
        presentation.opacity = timeline.opacity;
    }
    if (timeline.hasCornerRadius)
    {
        presentation.hasCornerRadius = true;
        presentation.cornerRadius = timeline.cornerRadius;
    }
    if (timeline.hasVisualOffset)
    {
        presentation.hasVisualOffset = true;
        presentation.visualOffset = {
            .x = timeline.visualOffset.x,
            .y = timeline.visualOffset.y,
        };
    }
    return presentation;
}

[[nodiscard]] UILayoutStyle UIContext::Impl::presentationLayoutStyle(u32 nodeIndex) const noexcept
{
    UILayoutStyle result = layoutStylesByIndex[nodeIndex];
    const auto presentation = timelineStorage.presentationFor(idForIndex(nodeIndex));
    if (presentation.hasLayoutWidth)
    {
        result.size.width = UILayoutLength::Px(presentation.layoutWidth);
    }
    if (presentation.hasLayoutHeight)
    {
        result.size.height = UILayoutLength::Px(presentation.layoutHeight);
    }
    if (presentation.hasLayoutOffset)
    {
        result.overlay.offset.x = UILayoutLength::Px(presentation.layoutOffset.x);
        result.overlay.offset.y = UILayoutLength::Px(presentation.layoutOffset.y);
    }
    return result;
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::currentBackgroundColor(UINodeId node, u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasBackgroundColor)
    {
        return presentation.backgroundColor;
    }
    if (nodeIndex < resolvedBoxFillCacheByNodeIndex.size())
    {
        return unpremultiplyColor(resolvedBoxFillCacheByNodeIndex[nodeIndex]);
    }
    if (boxPaintsByIndex[nodeIndex].solidFill.has_value())
    {
        return boxPaintsByIndex[nodeIndex].solidFill->color;
    }
    return {};
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::currentBorderColor(UINodeId node, u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasBorderColor)
    {
        return presentation.borderColor;
    }
    const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
    return paint.borderLight.alpha != 0 ? paint.borderLight : paint.borderDark;
}

[[nodiscard]] UIStraightSrgba8Color UIContext::Impl::currentTextColor(UINodeId node, u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasTextColor)
    {
        return presentation.textColor;
    }
    if (nodeIndex < textStatesByIndex.size() && textStatesByIndex[nodeIndex].hasContent)
    {
        return textStatesByIndex[nodeIndex].style.color;
    }
    return {};
}

[[nodiscard]] float UIContext::Impl::currentOpacity(UINodeId node, u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasOpacity)
    {
        return presentation.opacity;
    }
    if (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
        presentationOpacityValidByNodeIndex[nodeIndex] != 0)
    {
        return presentationOpacityByNodeIndex[nodeIndex];
    }
    return 1.0F;
}

[[nodiscard]] float UIContext::Impl::currentCornerRadius(UINodeId node, u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasCornerRadius)
    {
        return presentation.cornerRadius;
    }
    return boxPaintsByIndex[nodeIndex].cornerRadii.topLeft;
}

[[nodiscard]] Detail::UIMotionTrackStorage::Scalar2 UIContext::Impl::currentVisualOffset(UINodeId node,
                                                                        u32 nodeIndex) const noexcept
{
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasVisualOffset)
    {
        return presentation.visualOffset;
    }
    if (nodeIndex < presentationOffsetValidByNodeIndex.size() &&
        presentationOffsetValidByNodeIndex[nodeIndex] != 0)
    {
        return {.x = presentationOffsetXByNodeIndex[nodeIndex],
                .y = presentationOffsetYByNodeIndex[nodeIndex]};
    }
    // Default start for a new VisualOffset track is identity (no paint shift).
    // Authored drop-shadow offsets remain on UIBoxPaint.shadowOffset*.
    static_cast<void>(node);
    static_cast<void>(nodeIndex);
    return {};
}

[[nodiscard]] Core::Status UIContext::Impl::beginColorPropertyTransition(
    UINodeId node, UIAnimatableProperty property, UIStraightSrgba8Color target,
    const UITransitionSpec& spec)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (Core::Status validation = Detail::validateTransitionSpec(spec, property);
        !validation)
    {
        return validation;
    }
    if (timelineStorage.hasPresentationOwner(node, property))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Direct UI motion conflicts with an active keyframe timeline");
    }
    const u32 index = node.index();
    UIStraightSrgba8Color start{};
    switch (property)
    {
    case UIAnimatableProperty::BackgroundColor:
        start = currentBackgroundColor(node, index);
        break;
    case UIAnimatableProperty::BorderColor:
        start = currentBorderColor(node, index);
        break;
    case UIAnimatableProperty::TextColor:
        if (index >= textStatesByIndex.size() || !textStatesByIndex[index].hasContent)
        {
            return fail(UIErrorCode::InvalidText,
                        "UI text color transition requires intrinsic text content");
        }
        start = currentTextColor(node, index);
        break;
    default:
        return fail(UIErrorCode::InvalidStyle, "UI color transition property is unsupported");
    }
    const Core::MonotonicTimePoint now = motionNow();
    if (reducedMotionEnabled || spec.duration.count() <= 0.0)
    {
        if (Core::Status dirty =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirty)
        {
            return dirty;
        }
        applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
            .node = node,
            .property = property,
            .valueKind = Detail::UIMotionTrackStorage::ValueKind::Color,
            .color = target,
        });
        motionTrackStorage.cancelActiveProperty(node, property);
        return Core::success();
    }
    if (Core::Status status = motionTrackStorage.beginOrRetargetColor(
            node, property, start, target, spec, now);
        !status)
    {
        return status;
    }
    return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
}

[[nodiscard]] Core::Status UIContext::Impl::beginBackgroundColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return beginColorPropertyTransition(node, UIAnimatableProperty::BackgroundColor, target, spec);
}

[[nodiscard]] Core::Status UIContext::Impl::beginBorderColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return beginColorPropertyTransition(node, UIAnimatableProperty::BorderColor, target, spec);
}

[[nodiscard]] Core::Status UIContext::Impl::beginTextColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return beginColorPropertyTransition(node, UIAnimatableProperty::TextColor, target, spec);
}

[[nodiscard]] Core::Status UIContext::Impl::beginOpacityTransition(
    UINodeId node, float targetOpacity, const UITransitionSpec& spec)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (Core::Status validation = Detail::validateTransitionSpec(
            spec, UIAnimatableProperty::Opacity);
        !validation)
    {
        return validation;
    }
    if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::Opacity))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Direct UI motion conflicts with an active keyframe timeline");
    }
    if (!std::isfinite(targetOpacity))
    {
        return fail(UIErrorCode::InvalidStyle, "UI opacity target must be finite");
    }
    targetOpacity = std::clamp(targetOpacity, 0.0F, 1.0F);
    const u32 index = node.index();
    const float start = currentOpacity(node, index);
    const Core::MonotonicTimePoint now = motionNow();
    if (reducedMotionEnabled || spec.duration.count() <= 0.0)
    {
        if (Core::Status dirty =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirty)
        {
            return dirty;
        }
        applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
            .node = node,
            .property = UIAnimatableProperty::Opacity,
            .valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar,
            .scalar = targetOpacity,
        });
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::Opacity);
        return Core::success();
    }
    if (Core::Status status = motionTrackStorage.beginOrRetargetScalar(
            node, UIAnimatableProperty::Opacity, start, targetOpacity, spec, now);
        !status)
    {
        return status;
    }
    return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
}

[[nodiscard]] Core::Status UIContext::Impl::beginCornerRadiusTransition(
    UINodeId node, float targetRadius, const UITransitionSpec& spec)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (Core::Status validation = Detail::validateTransitionSpec(
            spec, UIAnimatableProperty::CornerRadius);
        !validation)
    {
        return validation;
    }
    const u32 index = node.index();
    if (boxPaintsByIndex[index].primitive != UIBoxPrimitiveKind::Rectangle)
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI corner-radius motion requires Rectangle box paint");
    }
    if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::CornerRadius))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Direct UI motion conflicts with an active keyframe timeline");
    }
    if (!std::isfinite(targetRadius) || targetRadius < 0.0F)
    {
        return fail(UIErrorCode::InvalidStyle, "UI corner radius target must be finite and non-negative");
    }
    const auto presentation = motionPresentationFor(node);
    if (!presentation.hasCornerRadius &&
        !boxPaintsByIndex[index].cornerRadii.isUniform())
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Uniform corner-radius motion cannot start from per-corner authored radii");
    }
    const float start = currentCornerRadius(node, index);
    const Core::MonotonicTimePoint now = motionNow();
    if (reducedMotionEnabled || spec.duration.count() <= 0.0)
    {
        if (Core::Status dirty =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirty)
        {
            return dirty;
        }
        applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
            .node = node,
            .property = UIAnimatableProperty::CornerRadius,
            .valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar,
            .scalar = targetRadius,
        });
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::CornerRadius);
        return Core::success();
    }
    if (Core::Status status = motionTrackStorage.beginOrRetargetScalar(
            node, UIAnimatableProperty::CornerRadius, start, targetRadius, spec, now);
        !status)
    {
        return status;
    }
    return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
}

[[nodiscard]] Core::Status UIContext::Impl::beginVisualOffsetTransition(
    UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (Core::Status validation = Detail::validateTransitionSpec(
            spec, UIAnimatableProperty::VisualOffset);
        !validation)
    {
        return validation;
    }
    if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::VisualOffset))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "Direct UI motion conflicts with an active keyframe timeline");
    }
    if (!std::isfinite(targetOffsetX) || !std::isfinite(targetOffsetY))
    {
        return fail(UIErrorCode::InvalidStyle, "UI visual offset target must be finite");
    }
    const u32 index = node.index();
    const auto start = currentVisualOffset(node, index);
    const Core::MonotonicTimePoint now = motionNow();
    const Detail::UIMotionTrackStorage::Scalar2 target{.x = targetOffsetX, .y = targetOffsetY};
    if (reducedMotionEnabled || spec.duration.count() <= 0.0)
    {
        if (Core::Status dirty =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirty)
        {
            return dirty;
        }
        applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
            .node = node,
            .property = UIAnimatableProperty::VisualOffset,
            .valueKind = Detail::UIMotionTrackStorage::ValueKind::Offset,
            .offset = target,
        });
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::VisualOffset);
        return Core::success();
    }
    if (Core::Status status = motionTrackStorage.beginOrRetargetOffset(
            node, UIAnimatableProperty::VisualOffset, start, target, spec, now);
        !status)
    {
        return status;
    }
    return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
}

[[nodiscard]] Core::Status UIContext::Impl::collectTimelineDirtyNodeVisitor(
    void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track)
{
    auto& context = *static_cast<TimelineDirtyNodeCollectionContext*>(rawContext);
    std::pmr::vector<UINodeId>* nodes = isLayoutAnimatableProperty(track.property)
                                                ? context.layoutNodes
                                                : context.paintNodes;
    if (nodes == nullptr || nodes->size() >= nodes->capacity())
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI timeline dirty-node scratch capacity has been exhausted");
    }
    nodes->push_back(track.node);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateTimelineTrackVisitor(
    void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track)
{
    auto& context = *static_cast<TimelineValidationContext*>(rawContext);
    Impl& impl = *context.impl;
    auto nodeResult = impl.resolveNode(track.node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (Core::Status capability =
            impl.validateTimelinePlaybackPropertyCapability(
                track.node, track.property);
        !capability)
    {
        return capability;
    }
    if (impl.motionTrackStorage.hasTrack(track.node, track.property))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI timeline property conflicts with a direct or persistent motion owner");
    }
    if (impl.timelineStorage.hasPresentationOwner(track.node, track.property, context.timeline))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI timeline property is already owned by another active timeline");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateTimelineNodes(UITimelineId timeline)
{
    TimelineValidationContext context{.impl = this, .timeline = timeline};
    return timelineStorage.visitTracks(timeline, &context, &validateTimelineTrackVisitor);
}

[[nodiscard]] Core::Status UIContext::Impl::collectTimelineDirtyNodes(UITimelineId timeline)
{
    timelineLayoutNodeScratch.clear();
    timelinePaintNodeScratch.clear();
    TimelineDirtyNodeCollectionContext context{
        .layoutNodes = &timelineLayoutNodeScratch,
        .paintNodes = &timelinePaintNodeScratch,
    };
    return timelineStorage.visitTracks(
        timeline, &context, &collectTimelineDirtyNodeVisitor);
}

[[nodiscard]] Core::Status UIContext::Impl::collectActiveTimelineDirtyNodes()
{
    timelineLayoutNodeScratch.clear();
    timelinePaintNodeScratch.clear();
    TimelineDirtyNodeCollectionContext context{
        .layoutNodes = &timelineLayoutNodeScratch,
        .paintNodes = &timelinePaintNodeScratch,
    };
    return timelineStorage.visitActiveTracks(
        &context, &collectTimelineDirtyNodeVisitor);
}

[[nodiscard]] Core::Status UIContext::Impl::markCollectedTimelineDirtyNodes()
{
    return markLayoutAndPaintDirtyBatch(
        std::span<const UINodeId>(timelineLayoutNodeScratch.data(),
                                  timelineLayoutNodeScratch.size()),
        std::span<const UINodeId>(timelinePaintNodeScratch.data(),
                                  timelinePaintNodeScratch.size()));
}

[[nodiscard]] Core::Status UIContext::Impl::validateTimelineDefinitionPropertyCapability(
    UINodeId node, UIAnimatableProperty property) const
{
    const u32 index = node.index();
    if (property == UIAnimatableProperty::TextColor &&
        (index >= textStatesByIndex.size() || !textStatesByIndex[index].hasContent))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI timeline text color track requires intrinsic text content");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateTimelinePlaybackPropertyCapability(
    UINodeId node, UIAnimatableProperty property) const
{
    if (Core::Status definitionCapability =
            validateTimelineDefinitionPropertyCapability(node, property);
        !definitionCapability)
    {
        return definitionCapability;
    }
    if (property == UIAnimatableProperty::CornerRadius &&
        boxPaintsByIndex[node.index()].primitive != UIBoxPrimitiveKind::Rectangle)
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI corner-radius timeline requires Rectangle box paint");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UITimelineId> UIContext::Impl::createTimeline(const UITimelineDesc& desc)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    // Validate the borrowed descriptor before copying it. The storage
    // repeats structural/keyframe validation and performs the capacity
    // preflight before publishing any slot.
    for (const UITimelineTrackDesc& track : desc.tracks)
    {
        auto nodeResult = resolveNode(track.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status capability =
                validateTimelineDefinitionPropertyCapability(
                    track.node, track.property);
            !capability)
        {
            return Core::failure(capability.error());
        }
    }
    return timelineStorage.create(desc);
}

[[nodiscard]] Core::Status UIContext::Impl::replaceTimeline(
    UITimelineId timeline, const UITimelineDesc& desc)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!timelineStorage.contains(timeline))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI timeline ID is invalid, stale, or belongs to another context");
    }
    for (const UITimelineTrackDesc& track : desc.tracks)
    {
        auto nodeResult = resolveNode(track.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status capability =
                validateTimelineDefinitionPropertyCapability(
                    track.node, track.property);
            !capability)
        {
            return capability;
        }
    }
    if (Core::Status preflight = timelineStorage.preflightReplace(timeline, desc); !preflight)
    {
        return preflight;
    }
    auto wasActive = timelineStorage.isActive(timeline);
    if (!wasActive)
    {
        return Core::failure(wasActive.error());
    }
    if (*wasActive)
    {
        if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
        {
            return collected;
        }
        if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
        {
            return dirty;
        }
    }
    if (Core::Status status = timelineStorage.replace(timeline, desc); !status)
    {
        return status;
    }
    const auto targets = timelineStorage.lastTargets();
    for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
    {
        applyTimelineTarget(target);
    }
    if (!targets.empty())
    {
        phaseDirty |= PhasePaint;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::playTimeline(UITimelineId timeline)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!timelineStorage.contains(timeline))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI timeline ID is invalid, stale, or belongs to another context");
    }
    if (Core::Status validation = validateTimelineNodes(timeline); !validation)
    {
        return validation;
    }
    if (!reducedMotionEnabled)
    {
        if (Core::Status preflight = timelineStorage.preflightPlay(timeline); !preflight)
        {
            return preflight;
        }
    }
    if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
    {
        return collected;
    }
    if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
    {
        return dirty;
    }
    if (reducedMotionEnabled)
    {
        if (Core::Status status = timelineStorage.snapToFinal(timeline); !status)
        {
            return status;
        }
    }
    else if (Core::Status status = timelineStorage.play(timeline, motionNow()); !status)
    {
        return status;
    }
    const auto targets = timelineStorage.lastTargets();
    for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
    {
        applyTimelineTarget(target);
    }
    phaseDirty |= PhasePaint;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::cancelTimeline(UITimelineId timeline)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto wasActive = timelineStorage.isActive(timeline);
    if (!wasActive)
    {
        return Core::failure(wasActive.error());
    }
    if (*wasActive)
    {
        if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
        {
            return collected;
        }
        if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
        {
            return dirty;
        }
    }
    if (Core::Status status = timelineStorage.cancel(timeline); !status)
    {
        return status;
    }
    const auto targets = timelineStorage.lastTargets();
    for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
    {
        applyTimelineTarget(target);
    }
    if (!targets.empty())
    {
        phaseDirty |= PhasePaint;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::destroyTimeline(UITimelineId timeline)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto wasActive = timelineStorage.isActive(timeline);
    if (!wasActive)
    {
        return Core::failure(wasActive.error());
    }
    if (*wasActive)
    {
        if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
        {
            return collected;
        }
        if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
        {
            return dirty;
        }
    }
    if (Core::Status status = timelineStorage.destroy(timeline); !status)
    {
        return status;
    }
    const auto targets = timelineStorage.lastTargets();
    for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
    {
        applyTimelineTarget(target);
    }
    if (!targets.empty())
    {
        phaseDirty |= PhasePaint;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isTimelineActive(UITimelineId timeline) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    return timelineStorage.isActive(timeline);
}

[[nodiscard]] Core::Status UIContext::Impl::sampleMotion(Core::MonotonicTimePoint now)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    const usize before = motionTrackStorage.activeCount();
    motionTrackStorage.beginCandidateSample(now);
    const usize timelineBefore = timelineStorage.activeCount();
    timelineStorage.beginCandidateSample(now);
    const auto layoutNodes = timelineStorage.candidateLayoutNodes();
    if (!layoutNodes.empty())
    {
        motionTrackStorage.ensureCandidateTransaction();
        if (Core::Status dirty = markLayoutDirtyBatch(layoutNodes); !dirty)
        {
            motionTrackStorage.rollbackCandidateSample();
            timelineStorage.discardCandidateSample();
            ++layoutTimelineCommitFailureCount;
            return dirty;
        }
        // Candidate presentation stays staged until commitLayout publishes
        // Layout, Hit, and Paint from this exact sample together.
        phaseDirty |= PhasePaint;
    }
    else
    {
        motionTrackStorage.commitCandidateSample();
        for (const Detail::UIMotionTrackStorage::Completed& completed :
             motionTrackStorage.lastCompleted())
        {
            applyCompletedMotion(completed);
        }
        timelineStorage.commitCandidateSample();
        const auto timelineTargets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
        {
            applyTimelineTarget(target);
        }
    }
    if (motionTrackStorage.lastSampledCount() == 0 && before == 0 &&
        timelineStorage.lastSampledTimelineCount() == 0 && timelineBefore == 0)
    {
        return Core::success();
    }
    phaseDirty |= PhasePaint;
    return Core::success();
}

[[nodiscard]] UIBoxPaint UIContext::Impl::presentationBoxPaint(UINodeId node, u32 nodeIndex) const noexcept
{
    UIBoxPaint paint = boxPaintsByIndex[nodeIndex];
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasBorderColor)
    {
        paint.borderLight = presentation.borderColor;
        paint.borderDark = presentation.borderColor;
    }
    if (presentation.hasCornerRadius)
    {
        paint.cornerRadii = UILogicalCornerRadii::uniform(
            presentation.cornerRadius);
    }
    // VisualOffset is applied to paint worldRect (not hit/layout) by callers.
    return paint;
}

[[nodiscard]] UILogicalRect UIContext::Impl::presentationPaintWorldRect(UINodeId node, u32 nodeIndex,
                                                       UILogicalRect worldRect) const noexcept
{
    // Only residual/active motion offsets shift chrome; default currentVisualOffset
    // falls back to box shadow offsets for transition *starts*, but paint shift
    // must not double-count authored drop-shadow offsets.
    const auto presentation = motionPresentationFor(node);
    float dx = 0.0F;
    float dy = 0.0F;
    if (presentation.hasVisualOffset)
    {
        dx = presentation.visualOffset.x;
        dy = presentation.visualOffset.y;
    }
    else if (nodeIndex < presentationOffsetValidByNodeIndex.size() &&
             presentationOffsetValidByNodeIndex[nodeIndex] != 0)
    {
        dx = presentationOffsetXByNodeIndex[nodeIndex];
        dy = presentationOffsetYByNodeIndex[nodeIndex];
    }
    if (dx == 0.0F && dy == 0.0F)
    {
        return worldRect;
    }
    worldRect.x = normalizeFloat(worldRect.x + dx);
    worldRect.y = normalizeFloat(worldRect.y + dy);
    return worldRect;
}

[[nodiscard]] UIPremultipliedRgba8Color UIContext::Impl::presentationBoxFill(UINodeId node,
                                                            u32 nodeIndex) const noexcept
{
    UIPremultipliedRgba8Color fill = resolvedBoxFillCacheByNodeIndex[nodeIndex];
    const auto presentation = motionPresentationFor(node);
    if (presentation.hasBackgroundColor)
    {
        fill = premultiply(presentation.backgroundColor);
    }
    float opacity = 1.0F;
    if (presentation.hasOpacity)
    {
        opacity = presentation.opacity;
    }
    else if (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
             presentationOpacityValidByNodeIndex[nodeIndex] != 0)
    {
        opacity = presentationOpacityByNodeIndex[nodeIndex];
    }
    if (opacity < 1.0F)
    {
        const u8 opacityByte = static_cast<u8>(std::clamp(opacity, 0.0F, 1.0F) * 255.0F + 0.5F);
        fill = applyOpacity(fill, opacityByte);
    }
    return fill;
}

} // namespace Tina::UI
