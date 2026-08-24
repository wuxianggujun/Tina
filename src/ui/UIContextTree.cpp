#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Status UIContext::Impl::reserveComponentBuildStorage(
    UIComponentBuildBudget budget, UIComponentBuildReservation& reservation)
{
    reservation = {};
    componentBuildNodeStatistics.requested += budget.nodes;
    if (componentBuildNodeStatistics.outstandingReservations > nodes.availableCount() ||
        budget.nodes > nodes.availableCount() - componentBuildNodeStatistics.outstandingReservations)
    {
        ++componentBuildNodeStatistics.capacityFailures;
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component node reservation exceeds remaining capacity");
    }

    componentBuildNodeStatistics.reserved += budget.nodes;
    componentBuildNodeStatistics.outstandingReservations += budget.nodes;
    reservation.remaining.nodes = budget.nodes;
    reservation.active = true;
    auto rollback = Core::makeScopeExit([this, &reservation]() noexcept {
        releaseComponentBuildStorage(reservation);
    });

    if (budget.textBytes > (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component text reservation exceeds the 32-bit text arena range");
    }
    auto textReservation = textStorage.reserve(static_cast<u32>(budget.textBytes));
    if (!textReservation)
    {
        return Core::failure(textReservation.error());
    }
    reservation.text = *textReservation;
    reservation.remaining.textBytes = budget.textBytes;

    auto canvasReservation = canvasCommandStorage.reserve(budget.canvasCommands);
    if (!canvasReservation)
    {
        return Core::failure(canvasReservation.error());
    }
    reservation.canvas = *canvasReservation;
    reservation.remaining.canvasCommands = budget.canvasCommands;

    const Detail::UIBehaviorStateSlotCounts behaviorCounts =
        toBehaviorSlotCounts(budget.behaviors);
    if (Core::Status behaviorReservation = behaviorStateStorage.reserve(behaviorCounts);
        !behaviorReservation)
    {
        return behaviorReservation;
    }
    reservation.behaviors = behaviorCounts;
    reservation.remaining.behaviors = budget.behaviors;
    rollback.release();
    return Core::success();
}

void UIContext::Impl::releaseComponentBuildStorage(UIComponentBuildReservation& reservation) noexcept
{
    if (!reservation.active)
    {
        return;
    }
    if (reservation.remaining.nodes > componentBuildNodeStatistics.outstandingReservations)
    {
        std::terminate();
    }
    componentBuildNodeStatistics.outstandingReservations -= reservation.remaining.nodes;
    textStorage.releaseReservation(reservation.text);
    canvasCommandStorage.releaseReservation(reservation.canvas);
    behaviorStateStorage.releaseReservation(reservation.behaviors);
    reservation = {};
}

[[nodiscard]] UIComponentBuildReservation*
UIContext::Impl::findComponentBuildReservation(UINodeId componentRoot) noexcept
{
    if (!componentRoot.hasValue() || componentRoot.index() >= componentBuildReservationsByNodeIndex.size())
    {
        return nullptr;
    }
    UIComponentBuildReservation& reservation =
        componentBuildReservationsByNodeIndex[componentRoot.index()];
    return reservation.active && reservation.componentRoot == componentRoot ? &reservation : nullptr;
}

[[nodiscard]] const UIComponentBuildReservation*
UIContext::Impl::findComponentBuildReservation(UINodeId componentRoot) const noexcept
{
    if (!componentRoot.hasValue() || componentRoot.index() >= componentBuildReservationsByNodeIndex.size())
    {
        return nullptr;
    }
    const UIComponentBuildReservation& reservation =
        componentBuildReservationsByNodeIndex[componentRoot.index()];
    return reservation.active && reservation.componentRoot == componentRoot ? &reservation : nullptr;
}

[[nodiscard]] bool UIContext::Impl::isBuildTransactionActive(UINodeId componentRoot) const noexcept
{
    return findComponentBuildReservation(componentRoot) != nullptr && contains(componentRoot);
}

[[nodiscard]] Core::Result<TextByteAllocation> UIContext::Impl::allocateRetainedText(u32 byteCount)
{
    if (activeComponentBuildReservation == nullptr)
    {
        return textStorage.allocate(byteCount);
    }
    auto allocation = textStorage.allocateReserved(activeComponentBuildReservation->text, byteCount);
    if (allocation)
    {
        activeComponentBuildReservation->remaining.textBytes =
            activeComponentBuildReservation->text.remainingBytes();
    }
    return allocation;
}

[[nodiscard]] Core::Status UIContext::Impl::assignRetainedCanvas(
    u32 nodeIndex, std::span<const UICanvasCommand> commands)
{
    if (activeComponentBuildReservation == nullptr)
    {
        return canvasCommandStorage.assign(nodeIndex, commands);
    }
    Core::Status status = canvasCommandStorage.assignReserved(
        nodeIndex, commands, activeComponentBuildReservation->canvas);
    if (status)
    {
        activeComponentBuildReservation->remaining.canvasCommands =
            activeComponentBuildReservation->canvas.remaining;
    }
    return status;
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::createNode(
    BuiltinElementKind kind, UIElementBehavior behaviors,
    std::optional<UIStyleRoleId> authoredStyleRole,
    std::span<const UIStyleClassId> authoredStyleClasses)
{
    if (activeComponentBuildReservation == nullptr)
    {
        const usize available = nodes.availableCount();
        if (available == 0)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI node capacity has been exhausted");
        }
        if (componentBuildNodeStatistics.outstandingReservations > available ||
            (componentBuildNodeStatistics.outstandingReservations != 0 &&
             available == componentBuildNodeStatistics.outstandingReservations))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI node capacity is reserved by active component builds");
        }
    }
    else if (activeComponentBuildReservation->remaining.nodes == 0 ||
             componentBuildNodeStatistics.outstandingReservations == 0)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component node reservation has been exhausted");
    }

    const UIStyleRoleId styleRole = authoredStyleRole.value_or(defaultStyleRoleForKind(kind));
    const bool reserveStyleMotion =
        needsStyleBackgroundMotionReservation(styleRole, authoredStyleClasses);
    if (reserveStyleMotion && motionTrackStorage.availableCount() == 0)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI style background transition reservation capacity has been exhausted");
    }

    auto idResult = nodes.tryEmplace();
    if (!idResult)
    {
        const Core::Error& error = idResult.error();
        if (error.code == Core::CoreErrorCode::CapacityExceeded)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI node capacity has been exhausted");
        }
        return Core::failure(error);
    }

    const UINodeId node = UINodeId::create(ownerWindow, *idResult);
    idsByIndex[node.index()] = node;
    resetNodeSideData(node.index());
    NodeRecord* record = nodes.tryGet(node.storageId());
    record->kind = kind;
    record->behaviors = behaviors;
    record->rootIndex = node.index();
    Core::Status published = activeComponentBuildReservation == nullptr
                                 ? behaviorStateStorage.publish(node.index(), behaviors)
                                 : behaviorStateStorage.publishReserved(
                                       node.index(), behaviors,
                                       activeComponentBuildReservation->behaviors);
    if (!published)
    {
        idsByIndex[node.index()] = {};
        static_cast<void>(nodes.erase(node.storageId()));
        resetNodeSideData(node.index());
        return Core::failure(published.error());
    }
    if (activeComponentBuildReservation != nullptr)
    {
        --activeComponentBuildReservation->remaining.nodes;
        --componentBuildNodeStatistics.outstandingReservations;
        ++componentBuildNodeStatistics.published;
        activeComponentBuildReservation->remaining.behaviors =
            toBehaviorSlotBudget(activeComponentBuildReservation->behaviors);
    }
    styleRolesByNodeIndex[node.index()] = styleRole;
    if (reserveStyleMotion)
    {
        if (Core::Status reserved =
                motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
            !reserved)
        {
            std::terminate();
        }
    }
    const auto semanticsDefaults = defaultSemanticsForKind(kind);
    semanticsStatesByNodeIndex[node.index()] = SemanticsState{
        .mode = semanticsDefaults.mode,
        .role = semanticsDefaults.role,
        .actions = semanticsDefaults.actions,
        .useContentAsName = semanticsDefaults.useContentAsName,
        .readOnly = semanticsDefaults.readOnly,
    };
    textStatesByIndex[node.index()].alignment = defaultContentAlignment(kind);
    if (kind == BuiltinElementKind::Modal || kind == BuiltinElementKind::Popup ||
        kind == BuiltinElementKind::Menu)
    {
        focusScopeModesByNodeIndex[node.index()] = UIFocusScopeMode::Contain;
    }
    if (kind == BuiltinElementKind::Popup || kind == BuiltinElementKind::Tooltip ||
        kind == BuiltinElementKind::Menu)
    {
        layoutStylesByIndex[node.index()].placement = UILayoutPlacement::Overlay;
    }
    styleRegistrationClosed = true;
    // Interactive controls are targetable. Label remains read-only and
    // decorative unless the caller explicitly changes its hit policy.
    pointerHitPoliciesByIndex[node.index()] =
        (hasBehavior(behaviors, UIElementBehavior::Focusable) ||
         hasBehavior(behaviors, UIElementBehavior::Activate) ||
         hasBehavior(behaviors, UIElementBehavior::RangeInput) ||
         hasBehavior(behaviors, UIElementBehavior::TextInput) ||
         hasBehavior(behaviors, UIElementBehavior::Scroll) ||
         hasBehavior(behaviors, UIElementBehavior::SelectOption))
            ? UIPointerHitPolicy::Targetable
            : UIPointerHitPolicy::Ignore;
    if (capacityConfig.applyDefaultProductChrome)
    {
        applyDefaultProductChrome(node.index(), styleRole);
    }
    return node;
}

[[nodiscard]] usize UIContext::Impl::availableNodeCountForCurrentCreation() const noexcept
{
    const usize available = nodes.availableCount();
    if (activeComponentBuildReservation != nullptr)
    {
        return (std::min)(available, activeComponentBuildReservation->remaining.nodes);
    }
    if (componentBuildNodeStatistics.outstandingReservations > available)
    {
        return 0;
    }
    return available - componentBuildNodeStatistics.outstandingReservations;
}

[[nodiscard]] Core::Status UIContext::Impl::initializeSemantics(
    u32 index, BuiltinElementKind kind, const UISemanticsDescriptor& descriptor,
    UIElementBehavior behaviors)
{
    if (Core::Status contract =
            validateSemanticsContract(descriptor, behaviors, kind);
        !contract)
    {
        return contract;
    }

    const std::string_view name = descriptor.name.value_or(std::string_view{});
    const std::string_view description = descriptor.description.value_or(std::string_view{});
    if (!Core::isStrictUtf8WithoutNul(name) || !Core::isStrictUtf8WithoutNul(description))
    {
        return fail(UIErrorCode::InvalidText, "UI semantics text must be strict UTF-8 without NUL");
    }
    if (name.size() > (std::numeric_limits<u32>::max)() ||
        description.size() > (std::numeric_limits<u32>::max)() - name.size())
    {
        return fail(UIErrorCode::CapacityExceeded, "UI semantics text payload is too large");
    }

    const u32 nameLength = static_cast<u32>(name.size());
    const u32 descriptionLength = static_cast<u32>(description.size());
    auto allocation = allocateRetainedText(nameLength + descriptionLength);
    if (!allocation)
    {
        return Core::failure(allocation.error());
    }
    textStorage.write(
        TextByteAllocation{
            .offset = allocation->offset,
            .capacity = nameLength,
        },
        name);
    textStorage.write(
        TextByteAllocation{
            .offset = allocation->offset + nameLength,
            .capacity = descriptionLength,
        },
        description);
    semanticsStatesByNodeIndex[index] = SemanticsState{
        .textAllocation = *allocation,
        .nameLength = nameLength,
        .descriptionLength = descriptionLength,
        .mode = descriptor.mode,
        .role = descriptor.role,
        .actions = descriptor.actions,
        .liveSetting = descriptor.liveSetting,
        .hasExplicitName = descriptor.name.has_value(),
        .hasExplicitDescription = descriptor.description.has_value(),
        .useContentAsName = descriptor.useContentAsName,
        .readOnly = descriptor.readOnly,
    };
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::initializeElement(UINodeId node, BuiltinElementKind kind,
                                             const UIElementDescriptor& descriptor,
                                             const UILayoutStyle& normalizedLayout)
{
    NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr)
    {
        return fail(UIErrorCode::InvalidNode, "UI element initialization references a stale node");
    }
    if (record->behaviors != descriptor.behaviors)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI element behavior state does not match its descriptor");
    }
    layoutStylesByIndex[node.index()] = normalizedLayout;
    enabledByNodeIndex[node.index()] = descriptor.enabled ? 1 : 0;
    const bool needsStyleMotion =
        needsStyleBackgroundMotionReservation(descriptor.visual.styleRole, descriptor.visual.styleClasses);
    const bool hasStyleMotion =
        motionTrackStorage.hasPersistentReservation(node, UIAnimatableProperty::BackgroundColor);
    if (needsStyleMotion && !hasStyleMotion)
    {
        if (motionTrackStorage.availableCount() == 0)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI style background transition reservation capacity has been exhausted");
        }
        if (Core::Status reserved =
                motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
            !reserved)
        {
            std::terminate();
        }
    } else if (!needsStyleMotion && hasStyleMotion)
    {
        motionTrackStorage.releasePersistentReservation(node, UIAnimatableProperty::BackgroundColor);
    }
    // Relationship-owned chrome must exist before applying role chrome so
    // the theme transition is not reset by later specialized initialization.
    if (kind == BuiltinElementKind::Tab)
    {
        if (!descriptor.tab.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI Tab is missing its retained configuration");
        }
        if (!tabViewStorage.initializeTab(node, *descriptor.tab))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Tab state capacity has been exhausted");
        }
    }
    if (kind == BuiltinElementKind::Splitter)
    {
        if (!descriptor.splitter.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI Splitter is missing its retained configuration");
        }
        if (!splitViewStorage.initializeSplitter(node, *descriptor.splitter))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Splitter state capacity has been exhausted");
        }
    }
    const u16 previousBindings = themeBindingsByNodeIndex[node.index()];
    const u16 nextBindings = capacityConfig.applyDefaultProductChrome
                                 ? defaultThemeBindingsFor(descriptor.visual.styleRole)
                                 : 0;
    styleRolesByNodeIndex[node.index()] = descriptor.visual.styleRole;
    const usize styleClassCount = descriptor.visual.styleClasses.size();
    std::copy(descriptor.visual.styleClasses.begin(), descriptor.visual.styleClasses.end(),
              styleClassesByNodeIndex[node.index()].begin());
    styleClassCountsByNodeIndex[node.index()] = static_cast<u8>(styleClassCount);
    activeNodeStyleClassLinkCount += styleClassCount;
    nodeStyleClassLinkHighWater =
        (std::max)(nodeStyleClassLinkHighWater, activeNodeStyleClassLinkCount);
    themeBindingsByNodeIndex[node.index()] = nextBindings;
    applyProductChromeTransition(node.index(), descriptor.visual.styleRole, productTheme,
                                 previousBindings | nextBindings, nextBindings);
    if (Core::Status semantics = initializeSemantics(
            node.index(), kind, descriptor.semantics, descriptor.behaviors);
        !semantics)
    {
        return semantics;
    }
    if (Core::Status canvas = assignRetainedCanvas(node.index(), descriptor.visual.canvas); !canvas)
    {
        return canvas;
    }
    if (descriptor.image.has_value())
    {
        if (Core::Status image = imageContentStorage.assign(node.index(), *descriptor.image); !image)
        {
            return image;
        }
        if ((nextBindings & ThemeBindingImageTint) != 0)
        {
            const UIStraightSrgba8Color tint =
                Detail::productChromeFor(descriptor.visual.styleRole, productTheme).imageTint;
            if (Core::Status tintStatus = imageContentStorage.setTint(node.index(), tint);
                !tintStatus)
            {
                return tintStatus;
            }
        }
    }
    if (descriptor.visual.boxPaint.has_value())
    {
        boxPaintsByIndex[node.index()] = normalizeBoxPaint(*descriptor.visual.boxPaint);
        detachThemeBinding(node.index(), ThemeBindingBoxPaint);
        styleOverridesByNodeIndex[node.index()] |= static_cast<u16>(UIStyleOverride::BoxPaint);
    }
    if (descriptor.pointerHitPolicy.has_value())
    {
        pointerHitPoliciesByIndex[node.index()] = *descriptor.pointerHitPolicy;
    }
    if (descriptor.focusScopeMode.has_value())
    {
        focusScopeModesByNodeIndex[node.index()] = *descriptor.focusScopeMode;
    }
    if (kind == BuiltinElementKind::Tooltip)
    {
        if (!descriptor.tooltip.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI Tooltip is missing its retained configuration");
        }
        if (!tooltipStorage.initializeTooltip(node, *descriptor.tooltip))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Tooltip state capacity has been exhausted");
        }
    }
    if (kind == BuiltinElementKind::SplitView)
    {
        if (!descriptor.splitView.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI SplitView is missing its retained configuration");
        }
        if (!splitViewStorage.initializeSplitView(node, *descriptor.splitView))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI SplitView state capacity has been exhausted");
        }
    }
    if (kind == BuiltinElementKind::TabView)
    {
        if (!descriptor.tabView.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI TabView is missing its retained configuration");
        }
        if (!tabViewStorage.initializeTabView(node, *descriptor.tabView))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI TabView state capacity has been exhausted");
        }
    }
    if (kind == BuiltinElementKind::Menu)
    {
        if (!descriptor.menu.has_value())
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI Menu is missing its retained configuration");
        }
        if (!menuStorage.initializeMenu(node, *descriptor.menu))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Menu state capacity has been exhausted");
        }
    }
    if (kind == BuiltinElementKind::MenuItem)
    {
        if (!descriptor.menuItem.has_value() || record->parentIndex == InvalidNodeIndex)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI MenuItem is missing its retained configuration or Menu parent");
        }
        if (!menuStorage.initializeMenuItem(
                node, idForIndex(record->parentIndex), *descriptor.menuItem))
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI MenuItem state capacity has been exhausted");
        }
        if (descriptor.menuItem->kind == UIMenuItemKind::Radio &&
            descriptor.menuItem->checked &&
            menuStorage.hasCheckedRadioPeer(node))
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI Menu cannot author multiple checked Radio items in one group");
        }
    }
    if (kind == BuiltinElementKind::Splitter)
    {
        Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(node.index());
        if (range == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI Splitter is missing RangeInput behavior state");
        }
        range->minValue = 0.0F;
        range->maxValue = 1.0F;
        range->step = descriptor.splitter->keyboardStep;
        range->value = 0.5F;
    }

    if (kind == BuiltinElementKind::TextEdit)
    {
        textEditMultilineByNodeIndex[node.index()] = descriptor.textEditMultiline;
        if (descriptor.textEditMultiline.enabled)
        {
            // Reserve the authored row budget while the element is being
            // created.  The commit path is noexcept and must only resize
            // this already-reserved vector; a late PMR allocation would
            // otherwise turn a bounded capacity failure into termination.
            try
            {
                const usize visualLineCapacity =
                    descriptor.textEditMultiline.maximumVisualLines != 0
                        ? descriptor.textEditMultiline.maximumVisualLines
                        : capacityConfig.textEditVisualLineCapacity;
                auto& visualLines = textEditVisualLinesByNodeIndex[node.index()];
                visualLines.reserve(visualLineCapacity);
                auto& candidateVisualLines =
                    candidateTextEditVisualLinesByNodeIndex[node.index()];
                candidateVisualLines.reserve(visualLineCapacity);
            }
            catch (const std::bad_alloc&)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI multiline TextEdit visual-line storage could not be reserved");
            }
        }
    }

    if (!descriptor.text.has_value())
    {
        refreshLocalPaintCache(node.index());
        static_cast<void>(refreshResolvedStyleCache(node.index()));
        return Core::success();
    }
    WidgetTextState& state = textStatesByIndex[node.index()];
    if (descriptor.textStyle.has_value())
    {
        state.style = *descriptor.textStyle;
        detachThemeBinding(node.index(), ThemeBindingTextStyle);
        styleOverridesByNodeIndex[node.index()] |= static_cast<u16>(UIStyleOverride::TextStyle);
    }
    state.alignment = descriptor.contentAlignment;

    const std::string_view text = *descriptor.text;
    auto metrics = measureWidgetText(text, state.style);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }
    if (text.empty())
    {
        state.metrics = {};
        refreshLocalPaintCache(node.index());
        static_cast<void>(refreshResolvedStyleCache(node.index()));
        return Core::success();
    }

    auto allocation = allocateRetainedText(static_cast<u32>(text.size()));
    if (!allocation)
    {
        return Core::failure(allocation.error());
    }
    textStorage.write(*allocation, text);
    state.allocation = *allocation;
    state.length = static_cast<u32>(text.size());
    state.metrics = *metrics;
    state.hasContent = true;
    refreshLocalPaintCache(node.index());
    static_cast<void>(refreshResolvedStyleCache(node.index()));
    return Core::success();
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::createElement(UINodeId parent,
                                                   const UIElementDescriptor& descriptor)
{
    auto kindResult = resolveElementBuiltinKind(descriptor);
    if (!kindResult)
    {
        return Core::failure(kindResult.error());
    }
    const BuiltinElementKind kind = *kindResult;

    auto normalizedLayout = normalizeLayoutStyle(descriptor.layout);
    if (!normalizedLayout)
    {
        return Core::failure(normalizedLayout.error());
    }
    if (descriptor.visual.boxPaint.has_value() &&
        !Detail::isValidLogicalCornerRadii(
            descriptor.visual.boxPaint->cornerRadii))
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI box corner radii must be finite and non-negative");
    }
    if (descriptor.pointerHitPolicy.has_value() && !isValidPointerHitPolicy(*descriptor.pointerHitPolicy))
    {
        return fail(UIErrorCode::InvalidPointerPolicy, "UI element pointer hit policy is not recognized");
    }
    if (descriptor.focusScopeMode.has_value() && !isValidFocusScopeMode(*descriptor.focusScopeMode))
    {
        return fail(UIErrorCode::InvalidFocusScope, "UI element focus-scope mode is not recognized");
    }
    if ((kind == BuiltinElementKind::Modal || kind == BuiltinElementKind::Popup ||
         kind == BuiltinElementKind::Menu) &&
        descriptor.focusScopeMode.has_value() && *descriptor.focusScopeMode != UIFocusScopeMode::Contain)
    {
        return fail(UIErrorCode::InvalidFocusScope,
                    "UI Modal, Popup, and Menu elements always contain focus");
    }
    if (kind != BuiltinElementKind::Tooltip && descriptor.tooltip.has_value())
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI Tooltip configuration requires the Tooltip contract");
    }
    if (kind != BuiltinElementKind::ListView && descriptor.listView != UIListViewCreateConfig{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI ListView creation config requires the VirtualList behavior");
    }
    if (kind != BuiltinElementKind::TreeView && descriptor.treeView != UITreeViewCreateConfig{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI TreeView creation config requires the VirtualTree behavior");
    }
    if (kind != BuiltinElementKind::VirtualGridView &&
        descriptor.virtualGridView != UIVirtualGridViewCreateConfig{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI VirtualGridView creation config requires the VirtualGrid behavior");
    }
    if (kind != BuiltinElementKind::DataGrid &&
        descriptor.dataGrid != UIDataGridCreateConfig{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI DataGrid creation config requires the DataGrid behavior");
    }
    if (kind == BuiltinElementKind::TextEdit)
    {
        const UITextEditMultilineConfig& multiline = descriptor.textEditMultiline;
        if (!std::isfinite(multiline.wheelStep) || multiline.wheelStep < 0.0F ||
            (multiline.enabled && multiline.maximumVisualLines == 0))
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI multiline TextEdit requires finite wheel step and visual-line capacity");
        }
        if (multiline.maximumBytes > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI multiline TextEdit byte limit is too large");
        }
        if (multiline.enabled &&
            static_cast<usize>(multiline.maximumVisualLines) > capacityConfig.textEditVisualLineCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI multiline TextEdit visual-line capacity exceeds the context budget");
        }
    }
    else if (descriptor.textEditMultiline != UITextEditMultilineConfig{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI multiline TextEdit configuration requires a TextEdit element");
    }
    if (!isValidStyleRole(descriptor.visual.styleRole))
    {
        return fail(UIErrorCode::InvalidElementDescriptor, "UI element style role is not recognized");
    }
    if (Core::Status classes = styleSheetStorage.validateClasses(
            descriptor.visual.styleClasses);
        !classes)
    {
        return Core::failure(classes.error());
    }
    if (activeNodeStyleClassLinkCount > capacityConfig.nodeStyleClassLinkCapacity ||
        descriptor.visual.styleClasses.size() >
            capacityConfig.nodeStyleClassLinkCapacity - activeNodeStyleClassLinkCount)
    {
        ++nodeStyleClassLinkCapacityFailureCount;
        return fail(UIErrorCode::CapacityExceeded,
                    "UI node style class link capacity has been exhausted");
    }
    if (descriptor.textStyle.has_value() && !descriptor.text.has_value())
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI element text style requires intrinsic text content");
    }
    if (descriptor.text.has_value() && descriptor.image.has_value())
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI element intrinsic text and image content are mutually exclusive");
    }
    UIElementDescriptor normalizedDescriptor = descriptor;
    if (descriptor.image.has_value())
    {
        auto image = normalizeImageContent(*descriptor.image);
        if (!image)
        {
            return Core::failure(image.error());
        }
        normalizedDescriptor.image = *image;
        normalizedDescriptor.contentAlignment = image->alignment;
        const bool isButtonImage = kind == BuiltinElementKind::Button;
        const bool isRadioButtonImage =
            kind == BuiltinElementKind::RadioButton;
        if (isButtonImage || isRadioButtonImage)
        {
            const UISemanticsRole expectedRole =
                isButtonImage ? UISemanticsRole::Button
                              : UISemanticsRole::RadioButton;
            const UISemanticsAction expectedActions =
                isButtonImage
                    ? UISemanticsAction::Focus |
                          UISemanticsAction::Activate
                    : UISemanticsAction::Focus |
                          UISemanticsAction::Activate |
                          UISemanticsAction::Toggle;
            if (descriptor.semantics.mode != UISemanticsMode::Publish ||
                descriptor.semantics.role != expectedRole ||
                descriptor.semantics.actions != expectedActions ||
                !descriptor.semantics.name.has_value() ||
                descriptor.semantics.name->empty() ||
                descriptor.semantics.useContentAsName)
            {
                return fail(
                    UIErrorCode::InvalidElementDescriptor,
                    "UI image controls require their control role, actions, and an explicit accessible name");
            }
        }
        const bool publishesAccessibleImage =
            !isButtonImage && !isRadioButtonImage &&
            descriptor.semantics.mode != UISemanticsMode::Exclude;
        if (publishesAccessibleImage &&
            (descriptor.semantics.role != UISemanticsRole::Image ||
             !descriptor.semantics.name.has_value() || descriptor.semantics.name->empty() ||
             descriptor.semantics.useContentAsName || descriptor.semantics.actions != UISemanticsAction::None))
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI accessible images require the Image role, an explicit name, and no actions");
        }
    }
    if (descriptor.text.has_value())
    {
        if (!isValidContentAlignment(descriptor.contentAlignment))
        {
            return fail(UIErrorCode::InvalidLayout,
                        "UI intrinsic content alignment supports Start, Center, or End on each axis");
        }
        if (descriptor.text->size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI element text payload is too large");
        }
        if ((kind == BuiltinElementKind::RadioButton && containsLineBreak(*descriptor.text)) ||
            (kind == BuiltinElementKind::TextEdit &&
             (descriptor.text->find('\r') != std::string_view::npos ||
              (!descriptor.textEditMultiline.enabled && descriptor.text->find('\n') != std::string_view::npos))))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI RadioButton and single-line TextEdit accept one logical line without CR or LF");
        }
        if (kind == BuiltinElementKind::TextEdit && descriptor.textEditMultiline.enabled &&
            descriptor.textEditMultiline.maximumVisualLines != 0)
        {
            const usize hardLineCount = 1U + static_cast<usize>(std::count(
                descriptor.text->begin(), descriptor.text->end(), '\n'));
            if (hardLineCount > descriptor.textEditMultiline.maximumVisualLines)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI multiline TextEdit visual-line capacity has been exceeded");
            }
        }
    } else if (!descriptor.image.has_value() && descriptor.contentAlignment != UIContentAlignment{})
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI element content alignment requires intrinsic text content");
    }

    Core::Result<UINodeId> nodeResult =
        kind == BuiltinElementKind::ListView
            ? createListViewComposite(parent, descriptor.listView, descriptor.visual.styleRole,
                                      descriptor.visual.styleClasses)
            : kind == BuiltinElementKind::TreeView
                  ? createTreeViewComposite(parent, descriptor.treeView, descriptor.visual.styleRole,
                                            descriptor.visual.styleClasses)
                  : kind == BuiltinElementKind::VirtualGridView
                        ? createVirtualGridViewComposite(
                              parent, descriptor.virtualGridView,
                              descriptor.visual.styleRole,
                              descriptor.visual.styleClasses)
                        : kind == BuiltinElementKind::DataGrid
                              ? createDataGridComposite(
                                    parent, descriptor.dataGrid,
                                    descriptor.visual.styleRole,
                                    descriptor.visual.styleClasses)
                        : createChild(parent, kind, descriptor.behaviors,
                                      descriptor.visual.styleRole,
                                      descriptor.visual.styleClasses);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }

    const UINodeId node = *nodeResult;
    auto rollback = Core::makeScopeExit([this, node]() noexcept {
        if (contains(node))
        {
            static_cast<void>(destroySubtree(node));
        }
    });
    if (Core::Status initialized = initializeElement(node, kind, normalizedDescriptor, *normalizedLayout); !initialized)
    {
        return Core::failure(initialized.error());
    }
    rollback.release();
    if (kind == BuiltinElementKind::Modal)
    {
        // A newly authored Modal changes the Window interaction scope. Keep
        // Tooltip presentation out of the same pending publication rather
        // than waiting for the new Modal to become the committed barrier.
        hardDismissAllTooltipsNoFail(true);
        if (const UINodeId activeMenuNode = menuStorage.rootMenu(); activeMenuNode.hasValue())
        {
            static_cast<void>(menuStorage.close(activeMenuNode));
            menuCommandPressLatch.clear();
            transientOverlayDismissPointerBarrierActive = false;
        }
    }
    return node;
}

[[nodiscard]] Core::Result<UIRootOwner> UIContext::Impl::createRoot(UIContext& context)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (liveRootCount >= capacityConfig.rootCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI root capacity has been exhausted");
    }

    auto nodeResult = createNode(BuiltinElementKind::Root,
                                 defaultBehaviorsForKind(BuiltinElementKind::Root));
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }

    const UINodeId root = *nodeResult;
    NodeRecord* rootRecord = nodes.tryGet(root.storageId());
    rootRecord->parentIndex = InvalidNodeIndex;
    rootRecord->previousSiblingIndex = lastRootIndex;
    rootRecord->nextSiblingIndex = InvalidNodeIndex;
    rootRecord->depth = 0;

    if (lastRootIndex != InvalidNodeIndex)
    {
        recordByIndex(lastRootIndex)->nextSiblingIndex = root.index();
    } else
    {
        firstRootIndex = root.index();
    }
    lastRootIndex = root.index();
    ++liveRootCount;
    markStructureChanged();
    return UIRootOwner(context.m_impl->lifetime, root);
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createChild(UINodeId parent, BuiltinElementKind kind,
            std::optional<UIElementBehavior> authoredBehaviors,
            std::optional<UIStyleRoleId> authoredStyleRole,
            std::span<const UIStyleClassId> authoredStyleClasses)
{
    if (kind == BuiltinElementKind::Root)
    {
        return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
    }
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();

    auto parentResult = resolveParent(parent);
    if (!parentResult)
    {
        return Core::failure(parentResult.error());
    }
    NodeRecord& parentRecord = **parentResult;
    if (parentRecord.kind == BuiltinElementKind::Tooltip ||
        parentRecord.kind == BuiltinElementKind::Splitter ||
        parentRecord.kind == BuiltinElementKind::Tab ||
        parentRecord.kind == BuiltinElementKind::MenuItem ||
        parentRecord.kind == BuiltinElementKind::VirtualGridViewItem ||
        parentRecord.kind == BuiltinElementKind::DataGridColumnHeader ||
        parentRecord.kind == BuiltinElementKind::DataGridCell)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tooltip, Tab, Splitter, and MenuItem elements cannot own child elements");
    }
    if (kind == BuiltinElementKind::Splitter &&
        parentRecord.kind != BuiltinElementKind::SplitView)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Splitter requires a SplitView parent");
    }
    if (kind == BuiltinElementKind::Tab &&
        parentRecord.kind != BuiltinElementKind::TabView)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tab requires a TabView parent");
    }
    if (parentRecord.kind == BuiltinElementKind::TabView &&
        tabViewStorage.relationshipValid(parent))
    {
        // Authoring may append children after an initial relationship was
        // published. Invalidate the old pair list before structure mutation;
        // callers must submit a new complete pair list before the next commit.
        tabViewStorage.unlinkTabView(parent);
    }
    if (parentRecord.kind == BuiltinElementKind::SplitView)
    {
        usize directChildCount = 0;
        for (u32 childIndex = parentRecord.firstChildIndex;
             childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            ++directChildCount;
            childIndex = child->nextSiblingIndex;
        }
        if (directChildCount >= 3)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView accepts exactly three direct parts");
        }
    }
    if (kind == BuiltinElementKind::Popup)
    {
        if (parentRecord.kind != BuiltinElementKind::Dropdown)
        {
            return fail(UIErrorCode::InvalidParent, "UI Popup requires a Dropdown parent");
        }
        if (parent.index() >= dropdownStatesByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Dropdown state index is out of range");
        }
        if (contains(dropdownStatesByNodeIndex[parent.index()].popup))
        {
            return fail(UIErrorCode::InvalidParent, "UI Dropdown already owns a Popup");
        }
    } else if (kind == BuiltinElementKind::Tooltip)
    {
        if (parentRecord.kind == BuiltinElementKind::Dropdown ||
            parentRecord.kind == BuiltinElementKind::Popup ||
            parentRecord.kind == BuiltinElementKind::ListView ||
            parentRecord.kind == BuiltinElementKind::TreeView ||
            parentRecord.kind == BuiltinElementKind::VirtualGridView ||
            parentRecord.kind == BuiltinElementKind::DataGrid)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Tooltip requires an ordinary retained parent");
        }
    } else if (kind == BuiltinElementKind::DropdownItem)
    {
        if (parentRecord.kind != BuiltinElementKind::Popup)
        {
            return fail(UIErrorCode::InvalidParent, "UI DropdownItem requires a Popup parent");
        }
    } else if (kind == BuiltinElementKind::MenuItem)
    {
        if (parentRecord.kind != BuiltinElementKind::Menu)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI MenuItem requires a Menu parent");
        }
    } else if (kind == BuiltinElementKind::Menu)
    {
        if (parentRecord.kind == BuiltinElementKind::Menu ||
            parentRecord.kind == BuiltinElementKind::MenuItem ||
            parentRecord.kind == BuiltinElementKind::Dropdown ||
             parentRecord.kind == BuiltinElementKind::Popup ||
             parentRecord.kind == BuiltinElementKind::ListView ||
             parentRecord.kind == BuiltinElementKind::TreeView ||
             parentRecord.kind == BuiltinElementKind::VirtualGridView ||
             parentRecord.kind == BuiltinElementKind::DataGrid)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Menu requires an ordinary retained parent");
        }
    } else if (kind == BuiltinElementKind::ListViewItem)
    {
        if (parentRecord.kind != BuiltinElementKind::ListView)
        {
            return fail(UIErrorCode::InvalidParent, "UI ListViewItem requires a ListView parent");
        }
    } else if (kind == BuiltinElementKind::TreeViewItem)
    {
        if (parentRecord.kind != BuiltinElementKind::TreeView)
        {
            return fail(UIErrorCode::InvalidParent, "UI TreeViewItem requires a TreeView parent");
        }
    } else if (kind == BuiltinElementKind::VirtualGridViewItem)
    {
        if (parentRecord.kind != BuiltinElementKind::VirtualGridView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI VirtualGridViewItem requires a VirtualGridView parent");
        }
    } else if (kind == BuiltinElementKind::DataGridColumnHeader)
    {
        if (parentRecord.kind != BuiltinElementKind::DataGrid)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI DataGridColumnHeader requires a DataGrid parent");
        }
    } else if (kind == BuiltinElementKind::DataGridRow)
    {
        if (parentRecord.kind != BuiltinElementKind::DataGrid)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI DataGridRow requires a DataGrid parent");
        }
    } else if (kind == BuiltinElementKind::DataGridCell)
    {
        if (parentRecord.kind != BuiltinElementKind::DataGridRow)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI DataGridCell requires a DataGridRow parent");
        }
    } else if (parentRecord.kind == BuiltinElementKind::TabView &&
               kind == BuiltinElementKind::TabView)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI TabView cannot directly contain another TabView");
    } else if (parentRecord.kind == BuiltinElementKind::Dropdown ||
               parentRecord.kind == BuiltinElementKind::Popup)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Dropdown composites only accept Popup and DropdownItem children");
    } else if (parentRecord.kind == BuiltinElementKind::ListView)
    {
        return fail(UIErrorCode::InvalidParent, "UI ListView only accepts its internal item rows");
    } else if (parentRecord.kind == BuiltinElementKind::TreeView)
    {
        return fail(UIErrorCode::InvalidParent, "UI TreeView only accepts its internal item rows");
    } else if (parentRecord.kind == BuiltinElementKind::VirtualGridView)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI VirtualGridView only accepts its internal item pool");
    } else if (parentRecord.kind == BuiltinElementKind::DataGrid ||
               parentRecord.kind == BuiltinElementKind::DataGridRow)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI DataGrid only accepts its internal fixed pools");
    } else if (parentRecord.kind == BuiltinElementKind::Menu)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Menu only accepts MenuItem children");
    }
    if (parentRecord.depth == (std::numeric_limits<u32>::max)())
    {
        return fail(UIErrorCode::InvalidParent, "UI parent depth cannot be represented");
    }

    auto nodeResult = createNode(kind, authoredBehaviors.value_or(defaultBehaviorsForKind(kind)),
                                 authoredStyleRole, authoredStyleClasses);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }

    const UINodeId node = *nodeResult;
    NodeRecord* childRecord = nodes.tryGet(node.storageId());
    childRecord->parentIndex = parent.index();
    childRecord->previousSiblingIndex = parentRecord.lastChildIndex;
    childRecord->nextSiblingIndex = InvalidNodeIndex;
    childRecord->rootIndex = parentRecord.rootIndex;
    childRecord->depth = parentRecord.depth + 1;

    if (parentRecord.lastChildIndex != InvalidNodeIndex)
    {
        recordByIndex(parentRecord.lastChildIndex)->nextSiblingIndex = node.index();
    } else
    {
        parentRecord.firstChildIndex = node.index();
    }
    parentRecord.lastChildIndex = node.index();
    if (kind == BuiltinElementKind::Popup)
    {
        dropdownStatesByNodeIndex[parent.index()].popup = node;
    }
    markStructureChanged();
    return node;
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createListViewComposite(UINodeId parent, UIListViewCreateConfig config,
                        UIStyleRoleId authoredStyleRole,
                        std::span<const UIStyleClassId> authoredStyleClasses)
{
    auto normalized = Detail::normalizeListViewCreateConfig(config);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
    if (availableNodeCountForCurrentCreation() < requiredNodes)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI ListView row pool exceeds the remaining node capacity");
    }

    auto listResult = createChild(parent, BuiltinElementKind::ListView, std::nullopt,
                                  authoredStyleRole, authoredStyleClasses);
    if (!listResult)
    {
        return Core::failure(listResult.error());
    }
    const UINodeId listView = *listResult;
    auto rollback = Core::makeScopeExit([this, listView]() noexcept {
        if (contains(listView))
        {
            static_cast<void>(destroySubtree(listView));
        }
    });
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    state.materializedItemCapacity = normalized->materializedItemCapacity;

    for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
    {
        auto itemResult = createChild(listView, BuiltinElementKind::ListViewItem);
        if (!itemResult)
        {
            return Core::failure(itemResult.error());
        }
        const UINodeId item = *itemResult;
        UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
        configureCollectionRowLayout(itemLayout, state.style.rowHeight);
        textStatesByIndex[item.index()].overflow = state.style.rowTextOverflow;
    }
    rollback.release();
    return listView;
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createVirtualGridViewComposite(
    UINodeId parent, UIVirtualGridViewCreateConfig config,
    UIStyleRoleId authoredStyleRole,
    std::span<const UIStyleClassId> authoredStyleClasses)
{
    auto normalized = normalizeVirtualGridViewCreateConfig(config);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    const auto requirements =
        Detail::resolveVirtualGridViewFixedPoolRequirements(*normalized);
    if (!requirements)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI VirtualGridView item pool size cannot be represented");
    }
    if (availableNodeCountForCurrentCreation() < requirements->totalNodes)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI VirtualGridView item pool exceeds the remaining node capacity");
    }
    if (virtualGridViewStorage.availableViewCount() == 0U ||
        requirements->items > virtualGridViewStorage.availableItemCount())
    {
        return fail(
            UIErrorCode::CapacityExceeded,
            "UI VirtualGridView exceeds its reserved component state capacity");
    }

    auto viewResult = createChild(
        parent, BuiltinElementKind::VirtualGridView, std::nullopt,
        authoredStyleRole, authoredStyleClasses);
    if (!viewResult)
    {
        return Core::failure(viewResult.error());
    }
    const UINodeId virtualGridView = *viewResult;
    auto rollback = Core::makeScopeExit([this, virtualGridView]() noexcept {
        if (contains(virtualGridView))
        {
            static_cast<void>(destroySubtree(virtualGridView));
        }
    });
    if (!virtualGridViewStorage.initializeView(virtualGridView, *normalized))
    {
        return fail(
            UIErrorCode::CapacityExceeded,
            "UI VirtualGridView state capacity has been exhausted");
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView state initialization failed");
    }

    virtualGridItemLinkScratch.clear();
    for (u32 ordinal = 0;
         ordinal < normalized->materializedItemCapacity; ++ordinal)
    {
        auto itemResult = createChild(
            virtualGridView, BuiltinElementKind::VirtualGridViewItem);
        if (!itemResult)
        {
            return Core::failure(itemResult.error());
        }
        const UINodeId item = *itemResult;
        configureCollectionRowLayout(
            layoutStylesByIndex[item.index()], state->style.itemHeight);
        textStatesByIndex[item.index()].overflow =
            state->style.itemTextOverflow;
        virtualGridItemLinkScratch.push_back(item);
    }
    if (!virtualGridViewStorage.linkMaterializedItems(
            virtualGridView, virtualGridItemLinkScratch))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView item pool linkage failed");
    }
    rollback.release();
    return virtualGridView;
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createDataGridComposite(
    UINodeId parent, UIDataGridCreateConfig config,
    UIStyleRoleId authoredStyleRole,
    std::span<const UIStyleClassId> authoredStyleClasses)
{
    const auto normalized = Detail::validateDataGridCreateConfig(config);
    if (!normalized)
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI DataGrid fixed-pool capacity is invalid");
    }
    const auto requirements =
        Detail::resolveDataGridFixedPoolRequirements(*normalized);
    if (!requirements)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid fixed-pool size cannot be represented");
    }
    if (availableNodeCountForCurrentCreation() < requirements->totalNodes)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid fixed pools exceed the remaining node capacity");
    }
    if (dataGridStorage.availableGridCount() == 0U ||
        requirements->columns > dataGridStorage.availableColumnCount() ||
        requirements->rows > dataGridStorage.availableRowCount() ||
        requirements->cells > dataGridStorage.availableCellCount())
    {
        return fail(
            UIErrorCode::CapacityExceeded,
            "UI DataGrid exceeds its reserved component state capacity");
    }

    auto gridResult = createChild(
        parent, BuiltinElementKind::DataGrid, std::nullopt,
        authoredStyleRole, authoredStyleClasses);
    if (!gridResult)
    {
        return Core::failure(gridResult.error());
    }
    const UINodeId dataGrid = *gridResult;
    auto rollback = Core::makeScopeExit([this, dataGrid]() noexcept {
        if (contains(dataGrid))
        {
            static_cast<void>(destroySubtree(dataGrid));
        }
    });
    if (!dataGridStorage.initializeGrid(dataGrid, *normalized))
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid state capacity has been exhausted");
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI DataGrid state initialization failed");
    }

    dataGridColumnLinkScratch.clear();
    dataGridRowLinkScratch.clear();
    dataGridCellLinkScratch.clear();
    for (u32 columnOrdinal = 0;
         columnOrdinal < normalized->columnCapacity; ++columnOrdinal)
    {
        auto columnResult = createChild(
            dataGrid, BuiltinElementKind::DataGridColumnHeader);
        if (!columnResult)
        {
            return Core::failure(columnResult.error());
        }
        const UINodeId column = *columnResult;
        configureCollectionRowLayout(
            layoutStylesByIndex[column.index()],
            state->style.columnHeaderHeight);
        textStatesByIndex[column.index()].overflow =
            state->style.headerTextOverflow;
        dataGridColumnLinkScratch.push_back(column);
    }
    for (u32 rowOrdinal = 0;
         rowOrdinal < normalized->materializedRowCapacity; ++rowOrdinal)
    {
        auto rowResult = createChild(
            dataGrid, BuiltinElementKind::DataGridRow);
        if (!rowResult)
        {
            return Core::failure(rowResult.error());
        }
        const UINodeId row = *rowResult;
        configureCollectionRowLayout(
            layoutStylesByIndex[row.index()], state->style.rowHeight);
        dataGridRowLinkScratch.push_back(row);
        for (u32 columnOrdinal = 0;
             columnOrdinal < normalized->columnCapacity; ++columnOrdinal)
        {
            auto cellResult = createChild(
                row, BuiltinElementKind::DataGridCell);
            if (!cellResult)
            {
                return Core::failure(cellResult.error());
            }
            const UINodeId cell = *cellResult;
            configureCollectionRowLayout(
                layoutStylesByIndex[cell.index()], state->style.rowHeight);
            textStatesByIndex[cell.index()].overflow =
                state->style.cellTextOverflow;
            dataGridCellLinkScratch.push_back(cell);
        }
    }
    if (!dataGridStorage.linkFixedPools(
            dataGrid, dataGridColumnLinkScratch,
            dataGridRowLinkScratch, dataGridCellLinkScratch))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI DataGrid fixed-pool linkage failed");
    }
    rollback.release();
    return dataGrid;
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createTreeViewComposite(UINodeId parent, UITreeViewCreateConfig config,
                        UIStyleRoleId authoredStyleRole,
                        std::span<const UIStyleClassId> authoredStyleClasses)
{
    auto normalized = Detail::normalizeTreeViewCreateConfig(config);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
    if (availableNodeCountForCurrentCreation() < requiredNodes)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI TreeView row pool exceeds the remaining node capacity");
    }

    auto treeResult = createChild(parent, BuiltinElementKind::TreeView, std::nullopt,
                                  authoredStyleRole, authoredStyleClasses);
    if (!treeResult)
    {
        return Core::failure(treeResult.error());
    }
    const UINodeId treeView = *treeResult;
    auto rollback = Core::makeScopeExit([this, treeView]() noexcept {
        if (contains(treeView))
        {
            static_cast<void>(destroySubtree(treeView));
        }
    });
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    state.materializedItemCapacity = normalized->materializedItemCapacity;

    for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
    {
        auto itemResult = createChild(treeView, BuiltinElementKind::TreeViewItem);
        if (!itemResult)
        {
            return Core::failure(itemResult.error());
        }
        const UINodeId item = *itemResult;
        UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
        configureCollectionRowLayout(itemLayout, state.style.rowHeight);
    }
    rollback.release();
    return treeView;
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::createElementFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                              const UIElementDescriptor& descriptor)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto parentResult = resolveParent(parent);
    if (!parentResult)
    {
        return Core::failure(parentResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, parent))
    {
        return fail(UIErrorCode::InvalidNode, "UI parent is not owned by the updater root");
    }

    return createElement(parent, descriptor);
}

[[nodiscard]] Core::Status UIContext::Impl::validateFlowUpdaterRoot(UINodeId updaterRoot) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI Flow requires a live updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::markFlowVisibilityDirty(std::initializer_list<UINodeId> screens)
{
    // A pending structure publication already forces a complete layout
    // rebuild, so startup registration/push needs no dirty-queue slots.
    if (isPhaseDirty(PhaseStructure))
    {
        return Core::success();
    }
    Core::Status status = markLayoutDirtyBatch(screens);
    if (!status && status.error().code == UIErrorCode::CapacityExceeded)
    {
        ++flowCapacityFailureCount;
    }
    return status;
}

[[nodiscard]] Core::Result<UIFlowLayerId> UIContext::Impl::registerFlowLayerFromUpdater(UINodeId updaterRoot,
                                                                       UINodeId layer)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) || layer == updaterRoot ||
        layer.index() >= flowStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidFlowLayer,
                    "UI Flow Layer must be a live node owned by the updater root");
    }
    const NodeRecord* layerRecord = nodes.tryGet(layer.storageId());
    if (layerRecord == nullptr || layerRecord->parentIndex != updaterRoot.index())
    {
        return fail(UIErrorCode::InvalidFlowLayer,
                    "UI Flow Layer must be a direct child of the updater root");
    }
    UIFlowNodeState& state = flowStatesByNodeIndex[layer.index()];
    if (state.kind != UIFlowNodeKind::None)
    {
        return fail(UIErrorCode::InvalidFlowLayer,
                    "UI Flow Layer node is already registered as a Layer or Screen");
    }
    if (registeredFlowLayerCount >= capacityConfig.flowLayerCapacity)
    {
        ++flowCapacityFailureCount;
        return fail(UIErrorCode::CapacityExceeded, "UI Flow Layer capacity has been exhausted");
    }

    state.kind = UIFlowNodeKind::Layer;
    ++registeredFlowLayerCount;
    flowLayerHighWater = (std::max)(flowLayerHighWater, registeredFlowLayerCount);
    return UIFlowLayerId{layer};
}

[[nodiscard]] Core::Result<UIFlowScreenId>
UIContext::Impl::registerFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowLayerId layerId, UINodeId screen)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    const UINodeId layer = layerId.nodeId();
    if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
        layer.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
    {
        return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
    }
    if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) || screen == updaterRoot ||
        screen.index() >= flowStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidFlowScreen,
                    "UI Flow Screen must be a live node owned by the updater root");
    }
    const NodeRecord* screenRecord = nodes.tryGet(screen.storageId());
    if (screenRecord == nullptr || screenRecord->parentIndex != layer.index())
    {
        return fail(UIErrorCode::InvalidFlowScreen,
                    "UI Flow Screen must be a direct child of its Layer");
    }
    UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
    if (state.kind != UIFlowNodeKind::None)
    {
        return fail(UIErrorCode::InvalidFlowScreen,
                    "UI Flow Screen node is already registered as a Layer or Screen");
    }
    if (registeredFlowScreenCount >= capacityConfig.flowScreenCapacity)
    {
        ++flowCapacityFailureCount;
        return fail(UIErrorCode::CapacityExceeded, "UI Flow Screen capacity has been exhausted");
    }
    if (Core::Status dirty = markFlowVisibilityDirty({screen}); !dirty)
    {
        return Core::failure(dirty.error());
    }

    state.kind = UIFlowNodeKind::Screen;
    state.layer = layer;
    ++registeredFlowScreenCount;
    flowScreenHighWater = (std::max)(flowScreenHighWater, registeredFlowScreenCount);
    return UIFlowScreenId{screen};
}

[[nodiscard]] Core::Status UIContext::Impl::pushFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return root;
    }
    const UINodeId screen = screenId.nodeId();
    if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
        screen.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
    {
        return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
    }
    UIFlowNodeState& screenState = flowStatesByNodeIndex[screen.index()];
    if (screenState.stacked)
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow Screen cannot be pushed more than once in its Layer stack");
    }
    if (!contains(screenState.layer) ||
        flowStatesByNodeIndex[screenState.layer.index()].kind != UIFlowNodeKind::Layer)
    {
        return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Screen Layer identity is stale");
    }
    UIFlowNodeState& layerState = flowStatesByNodeIndex[screenState.layer.index()];
    const UINodeId previousTop = layerState.top;
    if (Core::Status dirty = markFlowVisibilityDirty({previousTop, screen}); !dirty)
    {
        return dirty;
    }

    screenState.previous = previousTop;
    screenState.next = {};
    screenState.stacked = true;
    if (contains(previousTop))
    {
        flowStatesByNodeIndex[previousTop.index()].next = screen;
    }
    else
    {
        layerState.bottom = screen;
    }
    layerState.top = screen;
    ++stackedFlowScreenCount;
    flowStackHighWater = (std::max)(flowStackHighWater, stackedFlowScreenCount);
    return Core::success();
}

[[nodiscard]] Core::Result<UIFlowScreenId> UIContext::Impl::popFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                    UIFlowLayerId layerId)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    const UINodeId layer = layerId.nodeId();
    if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
        layer.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
    {
        return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
    }
    UIFlowNodeState& layerState = flowStatesByNodeIndex[layer.index()];
    const UINodeId popped = layerState.top;
    if (!contains(popped))
    {
        return fail(UIErrorCode::InvalidFlowOperation, "UI Flow Layer stack is empty");
    }
    UIFlowNodeState& poppedState = flowStatesByNodeIndex[popped.index()];
    const UINodeId nextTop = poppedState.previous;
    if (Core::Status dirty = markFlowVisibilityDirty({popped, nextTop}); !dirty)
    {
        return Core::failure(dirty.error());
    }

    if (contains(nextTop))
    {
        flowStatesByNodeIndex[nextTop.index()].next = {};
    }
    else
    {
        layerState.bottom = {};
    }
    layerState.top = nextTop;
    poppedState.previous = {};
    poppedState.next = {};
    poppedState.stacked = false;
    if (stackedFlowScreenCount == 0)
    {
        std::terminate();
    }
    --stackedFlowScreenCount;
    return UIFlowScreenId{popped};
}

[[nodiscard]] Core::Result<UIFlowScreenId> UIContext::Impl::replaceFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                        UIFlowScreenId replacementId)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    const UINodeId replacement = replacementId.nodeId();
    if (!contains(replacement) || !isNodeWithinRoot(updaterRoot, replacement) ||
        replacement.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[replacement.index()].kind != UIFlowNodeKind::Screen)
    {
        return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
    }
    UIFlowNodeState& replacementState = flowStatesByNodeIndex[replacement.index()];
    if (replacementState.stacked)
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow replacement Screen is already present in its Layer stack");
    }
    if (!contains(replacementState.layer) ||
        flowStatesByNodeIndex[replacementState.layer.index()].kind != UIFlowNodeKind::Layer)
    {
        return fail(UIErrorCode::InvalidFlowLayer, "UI Flow replacement Layer identity is stale");
    }
    UIFlowNodeState& layerState = flowStatesByNodeIndex[replacementState.layer.index()];
    const UINodeId replaced = layerState.top;
    if (!contains(replaced))
    {
        return fail(UIErrorCode::InvalidFlowOperation,
                    "UI Flow replace requires a non-empty Layer stack");
    }
    UIFlowNodeState& replacedState = flowStatesByNodeIndex[replaced.index()];
    const UINodeId previous = replacedState.previous;
    if (Core::Status dirty = markFlowVisibilityDirty({replaced, replacement}); !dirty)
    {
        return Core::failure(dirty.error());
    }

    replacementState.previous = previous;
    replacementState.next = {};
    replacementState.stacked = true;
    if (contains(previous))
    {
        flowStatesByNodeIndex[previous.index()].next = replacement;
    }
    else
    {
        layerState.bottom = replacement;
    }
    layerState.top = replacement;
    replacedState.previous = {};
    replacedState.next = {};
    replacedState.stacked = false;
    return UIFlowScreenId{replaced};
}

[[nodiscard]] Core::Result<UIFlowScreenId> UIContext::Impl::activeFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                       UIFlowLayerId layerId) const
{
    if (!isOwnerThread())
    {
        return fail(UIErrorCode::WrongOwnerThread, "UI Flow queries require the UI owner thread");
    }
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    const UINodeId layer = layerId.nodeId();
    if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
        layer.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
    {
        return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
    }
    return UIFlowScreenId{flowStatesByNodeIndex[layer.index()].top};
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isFlowScreenActiveFromUpdater(UINodeId updaterRoot,
                                                               UIFlowScreenId screenId) const
{
    if (!isOwnerThread())
    {
        return fail(UIErrorCode::WrongOwnerThread, "UI Flow queries require the UI owner thread");
    }
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return Core::failure(root.error());
    }
    const UINodeId screen = screenId.nodeId();
    if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
        screen.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
    {
        return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
    }
    return isActiveFlowScreenIndex(screen.index());
}

[[nodiscard]] Core::Status
UIContext::Impl::setFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                               UIFlowAction action, UIFlowActionCallback&& callback)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0 || flowActionCallbackOperationDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Flow action registration cannot change during input routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return root;
    }
    const usize actionIndex = flowActionSlotIndex(action);
    if (actionIndex >= FlowActionSlotCount || !callback.hasValue())
    {
        return fail(UIErrorCode::InvalidFlowAction,
                    "UI Flow Screen action requires a supported action and non-empty callback");
    }
    const UINodeId screen = screenId.nodeId();
    if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
        screen.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
    {
        return fail(UIErrorCode::InvalidFlowScreen,
                    "UI Flow Screen identity is stale or unregistered");
    }

    UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
    UIFlowActionSlot& actionSlot = state.actions[actionIndex];
    const bool replacing = actionSlot.registered;
    if (!replacing && registeredFlowActionCount >= capacityConfig.flowScreenCapacity)
    {
        ++flowCapacityFailureCount;
        return fail(UIErrorCode::CapacityExceeded,
                    "UI Flow Screen action capacity has been exhausted");
    }
    ++flowActionCallbackOperationDepth;
    auto callbackOperation = Core::makeScopeExit([this]() noexcept {
        --flowActionCallbackOperationDepth;
    });
    actionSlot.callback = std::move(callback);
    actionSlot.registered = true;
    if (!replacing)
    {
        ++registeredFlowActionCount;
        flowActionHighWater = (std::max)(flowActionHighWater, registeredFlowActionCount);
    }
    return Core::success();
}

[[nodiscard]] Core::Status
UIContext::Impl::clearFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                                 UIFlowAction action)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0 || flowActionCallbackOperationDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI Flow action registration cannot change during input routing");
    }
    drainDeferredRootDestroys();
    if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
    {
        return root;
    }
    const usize actionIndex = flowActionSlotIndex(action);
    if (actionIndex >= FlowActionSlotCount)
    {
        return fail(UIErrorCode::InvalidFlowAction,
                    "UI Flow Screen action is not supported by this router");
    }
    const UINodeId screen = screenId.nodeId();
    if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
        screen.index() >= flowStatesByNodeIndex.size() ||
        flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
    {
        return fail(UIErrorCode::InvalidFlowScreen,
                    "UI Flow Screen identity is stale or unregistered");
    }

    UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
    UIFlowActionSlot& actionSlot = state.actions[actionIndex];
    if (actionSlot.registered)
    {
        if (registeredFlowActionCount == 0)
        {
            std::terminate();
        }
        actionSlot.registered = false;
        --registeredFlowActionCount;
        ++flowActionCallbackOperationDepth;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept {
            --flowActionCallbackOperationDepth;
        });
        actionSlot.callback.reset();
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIComponentBuildBudget>
UIContext::Impl::requiredBuildBudgetForElement(const UIElementDescriptor& descriptor) const
{
    auto kind = resolveElementBuiltinKind(descriptor);
    if (!kind)
    {
        return Core::failure(kind.error());
    }
    UIComponentBuildBudget required{.nodes = 1};
    const auto addBehaviorSlots = [](UIBehaviorSlotBudget& slots,
                                     UIElementBehavior behaviors,
                                     usize count = 1U) noexcept {
        if (hasBehavior(behaviors, UIElementBehavior::Activate))
        {
            slots.activate += count;
        }
        if (hasBehavior(behaviors, UIElementBehavior::Toggle))
        {
            slots.toggle += count;
        }
        if (hasBehavior(behaviors, UIElementBehavior::RangeInput))
        {
            slots.range += count;
        }
        if (hasBehavior(behaviors, UIElementBehavior::TextInput))
        {
            slots.textInput += count;
        }
        if (hasBehavior(behaviors, UIElementBehavior::Scroll))
        {
            slots.scroll += count;
        }
        if (hasBehavior(behaviors, UIElementBehavior::Select))
        {
            slots.selection += count;
        }
    };
    addBehaviorSlots(required.behaviors, descriptor.behaviors);

    const usize semanticsNameBytes = descriptor.semantics.name.has_value()
                                         ? descriptor.semantics.name->size()
                                         : 0U;
    const usize semanticsDescriptionBytes = descriptor.semantics.description.has_value()
                                                ? descriptor.semantics.description->size()
                                                : 0U;
    const usize intrinsicTextBytes = descriptor.text.has_value() ? descriptor.text->size() : 0U;
    if (semanticsDescriptionBytes > (std::numeric_limits<usize>::max)() - semanticsNameBytes ||
        intrinsicTextBytes > (std::numeric_limits<usize>::max)() -
                                 semanticsNameBytes - semanticsDescriptionBytes)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component descriptor text budget overflowed");
    }
    required.textBytes = semanticsNameBytes + semanticsDescriptionBytes + intrinsicTextBytes;
    required.canvasCommands = descriptor.visual.canvas.size();

    if (*kind == BuiltinElementKind::ListView)
    {
        auto config = normalizeListViewCreateConfig(descriptor.listView);
        if (!config)
        {
            return Core::failure(config.error());
        }
        const usize rowCount = config->materializedItemCapacity;
        required.nodes += rowCount;
        addBehaviorSlots(required.behaviors,
                         defaultBehaviorsForKind(BuiltinElementKind::ListViewItem), rowCount);
    }
    else if (*kind == BuiltinElementKind::TreeView)
    {
        auto config = normalizeTreeViewCreateConfig(descriptor.treeView);
        if (!config)
        {
            return Core::failure(config.error());
        }
        const usize rowCount = config->materializedItemCapacity;
        required.nodes += rowCount;
        addBehaviorSlots(required.behaviors,
                         defaultBehaviorsForKind(BuiltinElementKind::TreeViewItem), rowCount);
    }
    else if (*kind == BuiltinElementKind::VirtualGridView)
    {
        auto config = normalizeVirtualGridViewCreateConfig(
            descriptor.virtualGridView);
        if (!config)
        {
            return Core::failure(config.error());
        }
        const usize itemCount = config->materializedItemCapacity;
        required.nodes += itemCount;
        addBehaviorSlots(
            required.behaviors,
            defaultBehaviorsForKind(BuiltinElementKind::VirtualGridViewItem),
            itemCount);
    }
    else if (*kind == BuiltinElementKind::DataGrid)
    {
        const auto requirements = Detail::resolveDataGridFixedPoolRequirements(
            descriptor.dataGrid);
        if (!requirements)
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI DataGrid fixed-pool capacity is invalid");
        }
        required.nodes += requirements->columns + requirements->rows +
                          requirements->cells;
        addBehaviorSlots(
            required.behaviors,
            defaultBehaviorsForKind(BuiltinElementKind::DataGridColumnHeader),
            requirements->columns);
        addBehaviorSlots(
            required.behaviors,
            defaultBehaviorsForKind(BuiltinElementKind::DataGridRow),
            requirements->rows);
        addBehaviorSlots(
            required.behaviors,
            defaultBehaviorsForKind(BuiltinElementKind::DataGridCell),
            requirements->cells);
    }
    return required;
}

[[nodiscard]] Core::Result<UIElementBuildTransaction>
UIContext::Impl::beginBuildTransaction(UIContext& context, UINodeId parent,
                      const UIElementDescriptor& rootDescriptor,
                      UIComponentBuildBudget budget)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    auto parentResult = resolveNode(parent);
    if (!parentResult)
    {
        return Core::failure(parentResult.error());
    }
    const u32 rootIndex = (*parentResult)->rootIndex;
    if (rootIndex >= idsByIndex.size() || !contains(idsByIndex[rootIndex]))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI component parent does not belong to a live root");
    }
    return beginBuildTransaction(
        context, idsByIndex[rootIndex], parent, rootDescriptor, budget);
}

[[nodiscard]] Core::Result<UIElementBuildTransaction>
UIContext::Impl::beginBuildTransaction(UIContext& context, UINodeId updaterRoot, UINodeId parent,
                      const UIElementDescriptor& rootDescriptor, UIComponentBuildBudget budget)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (budget.nodes == 0)
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI component build transaction requires a positive node budget");
    }
    auto requiredBudget = requiredBuildBudgetForElement(rootDescriptor);
    if (!requiredBudget)
    {
        ++componentBuildTransactionFailureCount;
        return Core::failure(requiredBudget.error());
    }
    if (!containsBudget(budget, *requiredBudget))
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component root exceeds its declared build budget");
    }

    UIComponentBuildReservation reservation;
    if (Core::Status reserved = reserveComponentBuildStorage(budget, reservation); !reserved)
    {
        ++componentBuildTransactionFailureCount;
        return Core::failure(reserved.error());
    }
    auto reservationRollback = Core::makeScopeExit([this, &reservation]() noexcept {
        releaseComponentBuildStorage(reservation);
    });

    if (activeComponentBuildReservation != nullptr)
    {
        ++componentBuildTransactionFailureCount;
        return fail(Core::CoreErrorCode::Internal,
                    "UI component build reservation scope is already active");
    }
    activeComponentBuildReservation = &reservation;
    auto activeScope = Core::makeScopeExit([this]() noexcept {
        activeComponentBuildReservation = nullptr;
    });
    auto componentRoot = createElementFromUpdater(updaterRoot, parent, rootDescriptor);
    if (!componentRoot)
    {
        ++componentBuildTransactionFailureCount;
        return Core::failure(componentRoot.error());
    }
    activeScope.release();
    activeComponentBuildReservation = nullptr;

    UIComponentBuildReservation& storedReservation =
        componentBuildReservationsByNodeIndex[componentRoot->index()];
    if (storedReservation.active)
    {
        static_cast<void>(destroySubtree(*componentRoot));
        ++componentBuildTransactionFailureCount;
        return fail(Core::CoreErrorCode::Internal,
                    "UI component root already owns a build reservation");
    }
    reservation.componentRoot = *componentRoot;
    storedReservation = std::move(reservation);
    reservation = {};
    reservationRollback.release();
    ++activeBuildTransactionCount;
    return UIElementBuildTransaction{
        context,
        updaterRoot,
        *componentRoot,
        storedReservation.remaining,
    };
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::createElementFromBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot, UINodeId parent,
                                  const UIElementDescriptor& descriptor,
                                  UIComponentBuildBudget& remainingBudget)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
    if (activeBuildTransactionCount == 0 || reservation == nullptr || !contains(updaterRoot) ||
        !contains(componentRoot) ||
        !isNodeWithinRoot(updaterRoot, componentRoot))
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is no longer active");
    }
    if (!contains(parent) || !isNodeWithinSubtree(componentRoot, parent))
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::InvalidParent,
                    "UI component build transaction parent must belong to its component subtree");
    }
    auto requiredBudget = requiredBuildBudgetForElement(descriptor);
    if (!requiredBudget)
    {
        ++componentBuildTransactionFailureCount;
        return Core::failure(requiredBudget.error());
    }
    if (!containsBudget(reservation->remaining, *requiredBudget))
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::CapacityExceeded,
                    "UI component build transaction budget has been exhausted");
    }

    if (activeComponentBuildReservation != nullptr)
    {
        ++componentBuildTransactionFailureCount;
        return fail(Core::CoreErrorCode::Internal,
                    "UI component build reservation scope is already active");
    }
    activeComponentBuildReservation = reservation;
    auto activeScope = Core::makeScopeExit([this]() noexcept {
        activeComponentBuildReservation = nullptr;
    });
    auto node = createElement(parent, descriptor);
    remainingBudget = reservation->remaining;
    if (!node)
    {
        ++componentBuildTransactionFailureCount;
        return Core::failure(node.error());
    }
    return node;
}

[[nodiscard]] Core::Status UIContext::Impl::commitBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                                  UIComponentBuildBudget& remainingBudget)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
    if (activeBuildTransactionCount == 0 || reservation == nullptr)
    {
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is no longer active");
    }
    if (!contains(updaterRoot) || !contains(componentRoot) || !isNodeWithinRoot(updaterRoot, componentRoot))
    {
        releaseComponentBuildStorage(*reservation);
        --activeBuildTransactionCount;
        remainingBudget = {};
        ++componentBuildTransactionFailureCount;
        return fail(UIErrorCode::InvalidNode, "UI component build transaction root is no longer alive");
    }
    releaseComponentBuildStorage(*reservation);
    --activeBuildTransactionCount;
    remainingBudget = {};
    return Core::success();
}

void UIContext::Impl::rollbackBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                              UIComponentBuildBudget& remainingBudget) noexcept
{
    if (!isOwnerThread())
    {
        std::terminate();
    }
    UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
    if (contains(updaterRoot) && contains(componentRoot) && isNodeWithinRoot(updaterRoot, componentRoot))
    {
        static_cast<void>(destroySubtree(componentRoot));
    }
    if (reservation != nullptr && reservation->active)
    {
        releaseComponentBuildStorage(*reservation);
        if (activeBuildTransactionCount != 0)
        {
            --activeBuildTransactionCount;
        }
    }
    remainingBudget = {};
}

void UIContext::Impl::unlinkFromTree(u32 index, NodeRecord& record) noexcept
{
    if (record.parentIndex != InvalidNodeIndex)
    {
        NodeRecord* parent = recordByIndex(record.parentIndex);
        if (parent != nullptr)
        {
            if (parent->firstChildIndex == index)
            {
                parent->firstChildIndex = record.nextSiblingIndex;
            }
            if (parent->lastChildIndex == index)
            {
                parent->lastChildIndex = record.previousSiblingIndex;
            }
        }
    } else
    {
        if (firstRootIndex == index)
        {
            firstRootIndex = record.nextSiblingIndex;
        }
        if (lastRootIndex == index)
        {
            lastRootIndex = record.previousSiblingIndex;
        }
    }

    if (record.previousSiblingIndex != InvalidNodeIndex)
    {
        if (NodeRecord* previous = recordByIndex(record.previousSiblingIndex); previous != nullptr)
        {
            previous->nextSiblingIndex = record.nextSiblingIndex;
        }
    }
    if (record.nextSiblingIndex != InvalidNodeIndex)
    {
        if (NodeRecord* next = recordByIndex(record.nextSiblingIndex); next != nullptr)
        {
            next->previousSiblingIndex = record.previousSiblingIndex;
        }
    }
}

void UIContext::Impl::eraseDetachedSubtree(u32 index) noexcept
{
    u32 currentIndex = index;
    while (currentIndex != InvalidNodeIndex)
    {
        NodeRecord* record = recordByIndex(currentIndex);
        if (record == nullptr)
        {
            return;
        }

        if (record->firstChildIndex != InvalidNodeIndex)
        {
            currentIndex = record->firstChildIndex;
            continue;
        }

        const u32 parentIndex = record->parentIndex;
        const u32 nextSiblingIndex = record->nextSiblingIndex;
        if (currentIndex != index)
        {
            unlinkFromTree(currentIndex, *record);
        }

        const UINodeId node = idForIndex(currentIndex);
        if (tooltipStorage.releaseNode(node, motionNow()))
        {
            markTooltipPresentationDirty();
        }
        static_cast<void>(dialogStorage.releaseNode(node));
        static_cast<void>(splitViewStorage.releaseNode(node));
        static_cast<void>(tabViewStorage.releaseNode(node));
        if (menuStorage.releaseNode(node))
        {
            menuCommandPressLatch.clear();
            transientOverlayDismissPointerBarrierActive = false;
        }
        if (record->kind == BuiltinElementKind::Modal)
        {
            hardDismissAllTooltipsNoFail(true);
        }
        if (record->kind == BuiltinElementKind::Modal && currentIndex < focusRestoreByNodeIndex.size())
        {
            const auto& committedEntries = committedHitBuffers[publishedHitBufferIndex];
            const u32 destroyedModalEntryIndex = findHitEntryIndex(node, committedEntries);
            if (committedActiveModalEntryIndex < committedEntries.size() &&
                destroyedModalEntryIndex < committedEntries.size() &&
                hitEntryIsWithinScope(committedActiveModalEntryIndex, destroyedModalEntryIndex, committedEntries))
            {
                // Post-order destruction visits the active Modal before its
                // committed Modal ancestors. The last stored value therefore
                // restores through every removed layer to the surviving scope.
                pendingDestroyedModalRestoreFocus = focusRestoreByNodeIndex[currentIndex];
                hasPendingDestroyedModalRestoreFocus = true;
            }
        }
        if (record->kind == BuiltinElementKind::DropdownItem)
        {
            const UINodeId dropdown = dropdownForItem(node);
            Detail::UISelectBehaviorState* select =
                dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
            if (select != nullptr && select->selectedOption == node)
            {
                select->selectedOption = {};
            }
        }
        if (record->kind == BuiltinElementKind::Popup)
        {
            const UINodeId dropdown = dropdownForPopup(node);
            if (dropdown.hasValue() && dropdown.index() < dropdownStatesByNodeIndex.size())
            {
                DropdownState& dropdownState = dropdownStatesByNodeIndex[dropdown.index()];
                if (dropdownState.popup == node)
                {
                    dropdownState.popup = {};
                    if (Detail::UISelectBehaviorState* select =
                            behaviorStateStorage.trySelectState(dropdown.index());
                        select != nullptr)
                    {
                        select->selectedOption = {};
                    }
                }
            }
            if (activePopupNode == node)
            {
                activePopupNode = {};
                transientOverlayDismissPointerBarrierActive = false;
                dropdownCommandPressLatch.clear();
            }
        }
        deactivateAllRoutedPointerListenersForNode(currentIndex);
        deactivateButtonActionForNode(currentIndex);
        sliderChangeCallbackRegistry.clearNode(currentIndex, true);
        timelineStorage.releaseNode(node);
        const auto timelineTargets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
        {
            applyTimelineTarget(target);
        }
        motionTrackStorage.releaseNode(node);
        releaseFlowNode(currentIndex);
        idsByIndex[currentIndex] = {};
        static_cast<void>(nodes.erase(node.storageId()));
        resetNodeSideData(currentIndex);
        routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
        buttonActionRegistry.reclaim(routeDispatchDepth != 0);
        sliderChangeCallbackRegistry.reclaim(routeDispatchDepth != 0);

        if (currentIndex == index)
        {
            return;
        }
        currentIndex = nextSiblingIndex != InvalidNodeIndex ? nextSiblingIndex : parentIndex;
    }
}

void UIContext::Impl::releaseComponentBuildReservationsInSubtree(UINodeId subtreeRoot) noexcept
{
    for (UIComponentBuildReservation& reservation : componentBuildReservationsByNodeIndex)
    {
        if (!reservation.active || !contains(reservation.componentRoot) ||
            !isNodeWithinSubtree(subtreeRoot, reservation.componentRoot))
        {
            continue;
        }
        releaseComponentBuildStorage(reservation);
        if (activeBuildTransactionCount == 0)
        {
            std::terminate();
        }
        --activeBuildTransactionCount;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::destroySubtree(UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }

    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }

    dispatchPointerCancelForSubtree(node);
    NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr)
    {
        // A PointerCancel listener may synchronously complete the requested
        // destruction. Generation validation makes that outcome idempotent.
        return Core::success();
    }
    releaseComponentBuildReservationsInSubtree(node);
    const bool wasRoot = record->kind == BuiltinElementKind::Root;
    unlinkFromTree(node.index(), *record);
    eraseDetachedSubtree(node.index());
    if (wasRoot && liveRootCount > 0)
    {
        --liveRootCount;
    }
    markStructureChanged();
    return Core::success();
}

void UIContext::Impl::destroyRootImmediately(UINodeId root) noexcept
{
    if (!isOwnerThread() || !contains(root))
    {
        return;
    }

    dispatchPointerCancelForSubtree(root);
    NodeRecord* rootRecord = nodes.tryGet(root.storageId());
    if (rootRecord == nullptr || rootRecord->kind != BuiltinElementKind::Root)
    {
        return;
    }

    releaseComponentBuildReservationsInSubtree(root);
    unlinkFromTree(root.index(), *rootRecord);
    eraseDetachedSubtree(root.index());
    if (liveRootCount > 0)
    {
        --liveRootCount;
    }
    markStructureChanged();
}

[[nodiscard]] Core::Status UIContext::Impl::destroyFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    if (!contains(node))
    {
        auto nodeResult = resolveNode(node);
        return nodeResult ? Core::success() : Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }

    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView item rows are internal and cannot be destroyed independently");
    }
    if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView item rows are internal and cannot be destroyed independently");
    }
    if (record != nullptr &&
        record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView items are internal and cannot be destroyed independently");
    }
    if (record != nullptr &&
        (record->kind == BuiltinElementKind::DataGridColumnHeader ||
         record->kind == BuiltinElementKind::DataGridRow ||
         record->kind == BuiltinElementKind::DataGridCell))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid fixed-pool nodes cannot be destroyed independently");
    }

    if (updaterRoot == node)
    {
        return fail(UIErrorCode::RootRequired, "Destroying a root node requires UIRootOwner::reset");
    }
    return destroySubtree(node);
}

[[nodiscard]] Core::Status UIContext::Impl::setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                     const UILayoutStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto normalizedStyle = Detail::normalizeLayoutStyle(style);
    if (!normalizedStyle)
    {
        return Core::failure(normalizedStyle.error());
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }

    const NodeRecord* nodeRecord = *nodeResult;
    if (dialogStorage.containsDialog(node) &&
        normalizedStyle->visibility != UIVisibility::Visible)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "Registered UI Dialog visibility is controlled by openDialog and dismissDialog");
    }
    if ((nodeRecord->kind == BuiltinElementKind::Tooltip ||
         nodeRecord->kind == BuiltinElementKind::Menu) &&
        normalizedStyle->placement != UILayoutPlacement::Overlay)
    {
        return fail(UIErrorCode::InvalidLayout,
                    "UI Tooltip and Menu nodes always require Overlay placement");
    }

    UILayoutStyle& currentStyle = layoutStylesByIndex[node.index()];
    if (currentStyle == *normalizedStyle)
    {
        return Core::success();
    }

    const bool hardDismissBarrier =
        nodeRecord->kind == BuiltinElementKind::Modal ||
        normalizedStyle->visibility != UIVisibility::Visible;
    const UINodeId menuToClose =
        hardDismissBarrier
            ? nodeRecord->kind == BuiltinElementKind::Modal
                  ? menuStorage.rootMenu()
                  : firstActiveMenuBranchAffectedBy(node, true)
            : UINodeId{};
    const bool closeActiveMenu = menuToClose.hasValue();
    const Core::Status dirtyStatus = closeActiveMenu
                                         ? markMenuMutationLayoutDirty(
                                               {node}, {menuToClose})
                                         : markStylePropertyDirty(
                                               node, UIStylePropertyKind::LayoutStyle);
    if (!dirtyStatus)
    {
        return dirtyStatus;
    }
    currentStyle = *normalizedStyle;
    if (closeActiveMenu)
    {
        if (Core::Status closed = setMenuOpenState(menuToClose, false); !closed)
        {
            return closed;
        }
    }
    if (hardDismissBarrier)
    {
        // Modal scope mutations and a Hidden/Collapsed endpoint or ancestor
        // are hard dismissal barriers. The next successful commit publishes
        // the visibility/layout/hit/paint/semantics transition atomically.
        hardDismissAllTooltipsNoFail(true);
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                          UIPointerHitPolicy policy)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    if (!isValidPointerHitPolicy(policy))
    {
        return fail(UIErrorCode::InvalidPointerPolicy, "UI pointer hit policy is not recognized");
    }
    if (((*nodeResult)->kind == BuiltinElementKind::Tooltip ||
         (*nodeResult)->kind == BuiltinElementKind::Menu) &&
        policy != UIPointerHitPolicy::Ignore)
    {
        return fail(UIErrorCode::InvalidPointerPolicy,
                    "UI Tooltip and Menu nodes always ignore Pointer hit testing");
    }

    UIPointerHitPolicy& currentPolicy = pointerHitPoliciesByIndex[node.index()];
    if (currentPolicy == policy)
    {
        return Core::success();
    }
    const bool clearHover = hoveredPrimaryControl == node && policy != UIPointerHitPolicy::Targetable;
    if (clearHover)
    {
        // Hover chrome is paint-only; hit policy itself is HitTest metadata.
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
    }
    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::PointerHitPolicy);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    currentPolicy = policy;
    if (clearHover)
    {
        clearHoveredPrimaryControl();
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const UISemanticsMode semanticsMode = semanticsStatesByNodeIndex[node.index()].mode;
    if (semanticsMode != UISemanticsMode::Publish && semanticsMode != UISemanticsMode::MergeDescendants)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
    }
    if (node.index() >= enabledByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
    }

    const u8 next = enabled ? 1 : 0;
    if (enabledByNodeIndex[node.index()] == next)
    {
        return Core::success();
    }

    UINodeId popupToClose{};
    UINodeId menuToClose{};
    if (!enabled)
    {
        if ((*nodeResult)->kind == BuiltinElementKind::Dropdown)
        {
            const UINodeId popup = popupForDropdown(node);
            if (popup.hasValue() && popupStatesByNodeIndex[popup.index()].open)
            {
                popupToClose = popup;
            }
        } else if ((*nodeResult)->kind == BuiltinElementKind::Popup &&
                   popupStatesByNodeIndex[node.index()].open)
        {
            popupToClose = node;
        }
        menuToClose = firstActiveMenuBranchAffectedBy(node, true);
    }

    // Dirty capacity is reserved before interaction state changes so a
    // rejected setter leaves enabled/focus/arm state untouched.
    releaseRouteDirtyQueueReservations();
    addRouteDirtyReservationCandidate(node);
    if (popupToClose.hasValue())
    {
        addRouteLayoutDirtyReservationCandidates(popupToClose);
        addRouteLayoutDirtyReservationCandidates(dropdownForPopup(popupToClose));
    }
    if (menuToClose.hasValue())
    {
        addActiveMenuBranchDirtyReservationCandidates(menuToClose);
    }
    if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
    {
        releaseRouteDirtyQueueReservations();
        return reservation;
    }
    auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
    if (Core::Status dirty = markPaintDirty(node); !dirty)
    {
        return dirty;
    }
    if (popupToClose.hasValue())
    {
        if (Core::Status closed = setPopupOpenState(popupToClose, false); !closed)
        {
            return closed;
        }
    }
    if (menuToClose.hasValue())
    {
        if (Core::Status closed = setMenuOpenState(menuToClose, false); !closed)
        {
            return closed;
        }
    }
    if (!enabled && capturedPointerNode == node)
    {
        dispatchPointerCancelForCurrentCapture();
        if (!contains(node))
        {
            return Core::success();
        }
    }
    enabledByNodeIndex[node.index()] = next;
    if (!enabled)
    {
        defaultActionPressState.clearNode(node);
        if (hoveredPrimaryControl == node)
        {
            hoveredPrimaryControl = {};
        }
        if (armedPrimaryButton == node)
        {
            clearArmedPrimaryButton();
        }
        if (armedSlider == node)
        {
            clearArmedSlider();
        }
        if (armedScrollView == node)
        {
            clearArmedScrollView();
        }
        if (armedTextEdit == node)
        {
            clearArmedTextEdit();
        }
        if (capturedPointerNode == node)
        {
            capturedPointerNode = {};
        }
        if (defaultActionFocusButton == node)
        {
            defaultActionFocusButton = {};
        }
        if (textInputFocus == node)
        {
            textInputFocus = {};
            resetTextEditPreferredX(node);
            resetImeCompositionState();
        }
        if (tooltipForAnchor(node).hasValue())
        {
            hardDismissAllTooltipsNoFail(true);
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const UISemanticsMode semanticsMode = semanticsStatesByNodeIndex[node.index()].mode;
    if (semanticsMode != UISemanticsMode::Publish && semanticsMode != UISemanticsMode::MergeDescendants)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
    }
    if (node.index() >= enabledByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
    }
    return enabledByNodeIndex[node.index()] != 0;
}

[[nodiscard]] Core::Status UIContext::Impl::setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
    }
    if (!isValidFocusScopeMode(mode))
    {
        return fail(UIErrorCode::InvalidFocusScope, "UI focus-scope mode is not recognized");
    }
    if (((*nodeResult)->kind == BuiltinElementKind::Modal || (*nodeResult)->kind == BuiltinElementKind::Popup) &&
        mode != UIFocusScopeMode::Contain)
    {
        return fail(UIErrorCode::InvalidFocusScope, "UI Modal and Popup nodes always contain focus");
    }
    if ((*nodeResult)->kind == BuiltinElementKind::Tooltip &&
        mode != UIFocusScopeMode::None)
    {
        return fail(UIErrorCode::InvalidFocusScope,
                    "UI Tooltip nodes never establish a focus scope");
    }
    UIFocusScopeMode& current = focusScopeModesByNodeIndex[node.index()];
    if (current == mode)
    {
        return Core::success();
    }
    if (Core::Status dirty = markHitTestDirty(node); !dirty)
    {
        return dirty;
    }
    current = mode;
    return Core::success();
}

[[nodiscard]] Core::Result<UIFocusScopeMode> UIContext::Impl::focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
    }
    return focusScopeModesByNodeIndex[node.index()];
}

[[nodiscard]] Core::Status UIContext::Impl::setStyleRoleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                   UIStyleRoleId role)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI style role is not owned by the updater root");
    }
    if (!isValidStyleRole(role))
    {
        return fail(UIErrorCode::InvalidTheme, "UI style role is not recognized");
    }

    const u32 index = node.index();
    if (styleRolesByNodeIndex[index] == role)
    {
        return Core::success();
    }
    const bool needsStyleMotion = needsStyleBackgroundMotionReservation(role, styleClassesFor(index));
    const bool hasStyleMotion =
        motionTrackStorage.hasPersistentReservation(node, UIAnimatableProperty::BackgroundColor);
    if (needsStyleMotion && !hasStyleMotion &&
        !motionTrackStorage.hasTrack(node, UIAnimatableProperty::BackgroundColor) &&
        motionTrackStorage.availableCount() == 0)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI style role requires an unavailable background transition reservation");
    }
    const u16 previousBindings = themeBindingsByNodeIndex[index];
    const u16 supportedBindings =
        capacityConfig.applyDefaultProductChrome ? defaultThemeBindingsFor(role) : 0;
    const u16 nextBindings =
        supportedBindings & static_cast<u16>(~styleOverridesByNodeIndex[index]);
    const u16 affectedBindings = previousBindings | nextBindings;

    std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
    if (Core::Status staged =
            stageProductChromeTransition(index, role, productTheme, affectedBindings, nextBindings);
        !staged)
    {
        return staged;
    }
    stageThemePaintChange(index);
    propagateThemeLayoutDirtyToAncestors();
    if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
    {
        return capacity;
    }

    motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BackgroundColor);
    if (needsStyleMotion && !hasStyleMotion)
    {
        if (Core::Status reserved =
                motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
            !reserved)
        {
            std::terminate();
        }
    } else if (!needsStyleMotion && hasStyleMotion)
    {
        motionTrackStorage.releasePersistentReservation(node, UIAnimatableProperty::BackgroundColor);
    }

    styleRolesByNodeIndex[index] = role;
    themeBindingsByNodeIndex[index] = nextBindings;
    applyStagedProductChromeTransition(index, role, productTheme, affectedBindings, nextBindings);
    publishThemeDirtyState();
    return Core::success();
}

[[nodiscard]] Core::Result<UIStyleRoleId> UIContext::Impl::styleRoleFromUpdater(UINodeId updaterRoot,
                                                               UINodeId node) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI style role is not owned by the updater root");
    }
    return styleRolesByNodeIndex[node.index()];
}

[[nodiscard]] Core::Status UIContext::Impl::clearOverrideFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                    UIStyleOverride properties)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI style override is not owned by the updater root");
    }

    constexpr u16 ValidOverrides = static_cast<u16>(UIStyleOverride::All);
    const u16 requested = static_cast<u16>(properties);
    if ((requested & static_cast<u16>(~ValidOverrides)) != 0)
    {
        return fail(UIErrorCode::InvalidTheme, "UI style override mask contains an unknown property");
    }
    const u32 index = node.index();
    const u16 overridesToClear = styleOverridesByNodeIndex[index] & requested;
    if (overridesToClear == 0)
    {
        return Core::success();
    }

    const UIStyleRoleId role = styleRolesByNodeIndex[index];
    const u16 supportedBindings =
        capacityConfig.applyDefaultProductChrome ? defaultThemeBindingsFor(role) : 0;
    const u16 restoredBindings = supportedBindings & overridesToClear;
    const u16 previousBindings = themeBindingsByNodeIndex[index];
    const u16 nextBindings = previousBindings | restoredBindings;
    const u16 affectedBindings = nextBindings & static_cast<u16>(~previousBindings);

    std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
    if (Core::Status staged =
            stageProductChromeTransition(index, role, productTheme, affectedBindings, affectedBindings);
        !staged)
    {
        return staged;
    }
    if ((overridesToClear & boxFillOverrideMask(**nodeResult)) != 0 ||
        (overridesToClear & static_cast<u16>(UIStyleOverride::ImageTint)) != 0)
    {
        stageThemePaintChange(index);
    }
    propagateThemeLayoutDirtyToAncestors();
    if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
    {
        return capacity;
    }

    styleOverridesByNodeIndex[index] &= static_cast<u16>(~requested);
    themeBindingsByNodeIndex[index] = nextBindings;
    applyStagedProductChromeTransition(index, role, productTheme, affectedBindings, affectedBindings);
    if ((overridesToClear & static_cast<u16>(UIStyleOverride::ImageTint)) != 0)
    {
        static_cast<void>(refreshResolvedStyleCache(index));
    }
    publishThemeDirtyState();
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    if (!contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    if (!Detail::isValidLogicalCornerRadii(paint.cornerRadii))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI box corner radii must be finite and non-negative");
    }
    if (timelineStorage.hasPresentationOwner(
            node, UIAnimatableProperty::BackgroundColor) ||
        timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::BorderColor) ||
        timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::CornerRadius))
    {
        return fail(UIErrorCode::InvalidStyle,
                    "UI box paint setter conflicts with an active keyframe timeline");
    }

    const UIBoxPaint normalizedPaint = Detail::normalizeBoxPaint(paint);
    UIBoxPaint& currentPaint = boxPaintsByIndex[node.index()];
    const bool hasLocalOverride =
        (styleOverridesByNodeIndex[node.index()] &
         static_cast<u16>(UIStyleOverride::BoxPaint)) != 0;
    const bool hasActiveBackgroundMotion =
        motionTrackStorage.findActive(node, UIAnimatableProperty::BackgroundColor) != nullptr;
    const bool hasActiveBorderMotion =
        motionTrackStorage.findActive(node, UIAnimatableProperty::BorderColor) != nullptr;
    const bool hasActiveCornerRadiusMotion =
        motionTrackStorage.findActive(node, UIAnimatableProperty::CornerRadius) != nullptr;
    const bool hasActiveBoxPaintMotion =
        hasActiveBackgroundMotion || hasActiveBorderMotion || hasActiveCornerRadiusMotion;
    if (currentPaint == normalizedPaint && hasLocalOverride && !hasActiveBoxPaintMotion)
    {
        return Core::success();
    }
    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BackgroundColor);
    motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BorderColor);
    motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::CornerRadius);
    if (currentPaint != normalizedPaint)
    {
        currentPaint = normalizedPaint;
    }
    detachThemeBinding(node.index(), ThemeBindingBoxPaint);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setImageTintFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                   UIStraightSrgba8Color tint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const UIImageContent* current = imageContentStorage.get(node.index());
    if (current == nullptr)
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI image tint requires retained image content");
    }
    const u32 index = node.index();
    const UIStraightSrgba8Color previousResolved = resolvedImageTintColor(index, *current);
    const bool alreadyLocal = hasLocalImageTintOverride(index);
    // Effective color unchanged: still detach sheet if needed, but no paint dirty.
    if (previousResolved == tint)
    {
        if (!alreadyLocal)
        {
            if (current->tint != tint)
            {
                if (Core::Status setTint = imageContentStorage.setTint(index, tint);
                    !setTint)
                {
                    return setTint;
                }
            }
            detachThemeBinding(index, ThemeBindingImageTint);
            setResolvedImageTintTokenDependency(index, {});
            if (index < resolvedImageTintValidByNodeIndex.size())
            {
                resolvedImageTintValidByNodeIndex[index] = 0;
                resolvedImageTintCacheByNodeIndex[index] = {};
            }
        }
        return Core::success();
    }
    // Dirty metadata: ImageTint → ColorOrOpacity (Paint only, no Measure).
    // Local setter detaches stylesheet image tint (override wins).
    if (Core::Status dirtyStatus =
            markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
        !dirtyStatus)
    {
        return dirtyStatus;
    }
    if (Core::Status setTint = imageContentStorage.setTint(index, tint); !setTint)
    {
        return setTint;
    }
    detachThemeBinding(index, ThemeBindingImageTint);
    setResolvedImageTintTokenDependency(index, {});
    if (index < resolvedImageTintValidByNodeIndex.size())
    {
        resolvedImageTintValidByNodeIndex[index] = 0;
        resolvedImageTintCacheByNodeIndex[index] = {};
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIStraightSrgba8Color> UIContext::Impl::imageTintFromUpdater(UINodeId updaterRoot,
                                                                       UINodeId node) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, node))
    {
        return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
    }
    const UIImageContent* current = imageContentStorage.get(node.index());
    if (current == nullptr)
    {
        return fail(UIErrorCode::InvalidElementDescriptor,
                    "UI image tint requires retained image content");
    }
    return resolvedImageTintColor(node.index(), *current);
}

[[nodiscard]] Core::Status UIContext::Impl::setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                     const UIButtonPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto buttonResult = resolvePlainButton(button);
    if (!buttonResult)
    {
        return Core::failure(buttonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
    }
    if (button.index() >= buttonPaintsByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
    }
    UIButtonPaint& currentPaint = buttonPaintsByNodeIndex[button.index()];
    if (currentPaint == paint)
    {
        if ((styleOverridesByNodeIndex[button.index()] &
             static_cast<u16>(UIStyleOverride::ButtonPaint)) != 0)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(button); !dirty)
        {
            return dirty;
        }
        detachThemeBinding(button.index(), ThemeBindingButtonPaint);
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(button); !dirty)
    {
        return dirty;
    }
    currentPaint = paint;
    detachThemeBinding(button.index(), ThemeBindingButtonPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIButtonPaint> UIContext::Impl::buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto buttonResult = const_cast<Impl*>(this)->resolvePlainButton(button);
    if (!buttonResult)
    {
        return Core::failure(buttonResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, button))
    {
        return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
    }
    if (button.index() >= buttonPaintsByNodeIndex.size())
    {
        return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
    }
    return buttonPaintsByNodeIndex[button.index()];
}

} // namespace Tina::UI
