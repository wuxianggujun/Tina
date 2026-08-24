#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] usize UIContext::Impl::countCanvasPaintEntries(const UICommittedLayoutEntry& layoutEntry) const noexcept
{
    usize count = 0;
    const auto drawable = [&layoutEntry](const UICanvasCommand& command) noexcept {
        if (command.color.alpha == 0)
        {
            return false;
        }
        if (command.kind == UICanvasCommandKind::SolidLine)
        {
            return resolveCommittedLineGeometry(
                       UILineGeometry{
                           .start = command.lineStart,
                           .end = command.lineEnd,
                           .thickness = command.lineThickness,
                       },
                       {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y})
                .has_value();
        }
        return command.bounds.width > 0.0F && command.bounds.height > 0.0F;
    };
    canvasCommandStorage.forEach(layoutEntry.node.index(), [&](const UICanvasCommand& command) noexcept {
        if (!drawable(command))
        {
            return;
        }
        if (command.kind == UICanvasCommandKind::NineSlice)
        {
            count += makeNineSlicePatches(layoutEntry.worldRect, command).count;
        } else
        {
            ++count;
        }
    });
    return count;
}

void UIContext::Impl::appendCanvasPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                        const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept
{
    const u32 nodeIndex = layoutEntry.node.index();
    const UILogicalRect canvasClip = intersectRects(layoutEntry.effectiveClip, layoutEntry.worldRect);
    const NodeRecord* record = recordByIndex(nodeIndex);
    const UINodeId root = record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    const auto localRect = [&layoutEntry](const UICanvasCommand& command) noexcept {
        return UILogicalRect{
            .x = normalizeFloat(layoutEntry.worldRect.x + command.bounds.x),
            .y = normalizeFloat(layoutEntry.worldRect.y + command.bounds.y),
            .width = command.bounds.width,
            .height = command.bounds.height,
        };
    };
    const auto drawable = [&layoutEntry](const UICanvasCommand& command) noexcept {
        if (command.color.alpha == 0)
        {
            return false;
        }
        if (command.kind == UICanvasCommandKind::SolidLine)
        {
            return resolveCommittedLineGeometry(
                       UILineGeometry{
                           .start = command.lineStart,
                           .end = command.lineEnd,
                           .thickness = command.lineThickness,
                       },
                       {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y})
                .has_value();
        }
        return command.bounds.width > 0.0F && command.bounds.height > 0.0F;
    };
    const auto appendImage = [&](const UICanvasCommand& command, UILogicalRect worldRect,
                                 UIImagePixelRect sourcePixels,
                                 UICommittedImageBoundsProjection boundsProjection,
                                 UILogicalPoint projectionEnd) noexcept {
        UIImageSource source = command.imageSource;
        source.sourcePixels = sourcePixels;
        output.push_back(UICommittedPaintEntry{
            .node = layoutEntry.node,
            .root = root,
            .worldRect = worldRect,
            .effectiveClip = canvasClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(command.color),
            .kind = UICommittedPaintKind::Image,
            .imageSource = source,
            .imageSampling = command.imageSampling,
            .imageBoundsProjection = boundsProjection,
            .imageProjectionEnd = projectionEnd,
        });
        ++nextPaintOrdinal;
    };
    canvasCommandStorage.forEach(nodeIndex, [&](const UICanvasCommand& command) noexcept {
        if (!drawable(command))
        {
            return;
        }
        if (command.kind == UICanvasCommandKind::SolidRect)
        {
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .root = root,
                .worldRect = localRect(command),
                .effectiveClip = canvasClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = premultiply(command.color),
                .cornerRadii = command.cornerRadii,
            });
            ++nextPaintOrdinal;
        } else if (command.kind == UICanvasCommandKind::SolidEllipse)
        {
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .root = root,
                .worldRect = localRect(command),
                .effectiveClip = canvasClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = premultiply(command.color),
                .kind = UICommittedPaintKind::SolidEllipse,
                .ellipseStrokeWidth = command.ellipseStrokeWidth,
            });
            ++nextPaintOrdinal;
        } else if (command.kind == UICanvasCommandKind::SolidLine)
        {
            const auto geometry = resolveCommittedLineGeometry(
                UILineGeometry{
                    .start = command.lineStart,
                    .end = command.lineEnd,
                    .thickness = command.lineThickness,
                },
                {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y});
            if (!geometry)
            {
                return;
            }
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .root = root,
                .worldRect = geometry->worldEnvelope,
                .effectiveClip = canvasClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = premultiply(command.color),
                .kind = UICommittedPaintKind::SolidLine,
                .lineStart = geometry->worldStart,
                .lineEnd = geometry->worldEnd,
                .lineThickness = command.lineThickness,
            });
            ++nextPaintOrdinal;
        } else if (command.kind == UICanvasCommandKind::Image)
        {
            appendImage(
                command,
                localRect(command),
                command.imageSource.sourcePixels,
                UICommittedImageBoundsProjection::Cover,
                {});
        } else if (command.kind == UICanvasCommandKind::NineSlice)
        {
            const UINineSlicePatchBatch patches = makeNineSlicePatches(layoutEntry.worldRect, command);
            for (usize patchIndex = 0; patchIndex < patches.count; ++patchIndex)
            {
                const UINineSlicePatch& patch = patches.patches[patchIndex];
                appendImage(command, patch.worldRect, patch.sourcePixels,
                            UICommittedImageBoundsProjection::SharedBoundary, patch.worldEnd);
            }
        }
    });
}

[[nodiscard]] Core::Result<Detail::UIControlPaintBatch>
UIContext::Impl::resolveControlPaintBatch(const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity) const
{
    Detail::UIControlPaintBatch batch;
    bool batchCapacityExceeded = false;
    const auto add = [&](UILogicalRect worldRect, UIPremultipliedRgba8Color color,
                         UILogicalCornerRadii cornerRadii = {}) noexcept {
        batchCapacityExceeded = !batch.add(worldRect, color, cornerRadii) ||
                                batchCapacityExceeded;
    };
    const auto controlColor = [&](UIStraightSrgba8Color color) noexcept {
        const UIPremultipliedRgba8Color premultiplied = premultiply(color);
        return applyDisabledOpacity ? widgetPaintColor(layoutEntry.node, premultiplied) : premultiplied;
    };
    const auto addGridLines = [&](UILogicalRect rect,
                                  UIStraightSrgba8Color color) noexcept {
        if (color.alpha == 0 || rect.width <= 0.0F || rect.height <= 0.0F)
        {
            return;
        }
        constexpr float GridLineThickness = 1.0F;
        const float verticalThickness =
            (std::min)(GridLineThickness, rect.width);
        const float horizontalThickness =
            (std::min)(GridLineThickness, rect.height);
        const UIPremultipliedRgba8Color resolved = controlColor(color);
        add(UILogicalRect{
                .x = normalizeFloat(rect.right() - verticalThickness),
                .y = rect.y,
                .width = verticalThickness,
                .height = rect.height,
            },
            resolved);
        add(UILogicalRect{
                .x = rect.x,
                .y = normalizeFloat(rect.bottom() - horizontalThickness),
                .width = rect.width,
                .height = horizontalThickness,
            },
            resolved);
    };

    const u32 nodeIndex = layoutEntry.node.index();
    const NodeRecord* record = recordByIndex(nodeIndex);
    const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
    if (record != nullptr && record->kind == BuiltinElementKind::Checkbox &&
        nodeIndex < checkboxPaintsByNodeIndex.size() && toggleValue != nullptr)
    {
        const UICheckboxPaint& paint = checkboxPaintsByNodeIndex[nodeIndex];
        const bool checked = *toggleValue != 0;
        if (paint.presentation == UIToggleIndicatorPresentation::Switch)
        {
            const Detail::UISwitchGeometry geometry =
                Detail::resolveSwitchGeometry(
                    layoutEntry.worldRect, paint.checkedIndicatorInset, checked);
            const UIStraightSrgba8Color thumbColor =
                checked ? paint.checkedIndicatorColor
                        : paint.uncheckedIndicatorColor;
            batchCapacityExceeded =
                !batch.add(geometry.thumb, controlColor(thumbColor),
                           geometry.thumbCornerRadii) ||
                batchCapacityExceeded;
        } else if (checked)
        {
            const float extent =
                (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
            const float inset = paint.checkedIndicatorInset;
            add(
                UILogicalRect{
                    .x = normalizeFloat(layoutEntry.worldRect.x + inset),
                    .y = normalizeFloat(layoutEntry.worldRect.y + inset),
                    .width = normalizeFloat(extent - inset * 2.0F),
                    .height = normalizeFloat(extent - inset * 2.0F),
                },
                controlColor(paint.checkedIndicatorColor));
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::Slider &&
               nodeIndex < sliderPaintsByNodeIndex.size())
    {
        const Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(nodeIndex);
        if (range == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI Slider is missing RangeInput behavior state");
        }
        const UISliderPaint& paint = sliderPaintsByNodeIndex[nodeIndex];
        const SliderPaintGeometry geometry = sliderPaintGeometry(layoutEntry.worldRect, range->minValue,
                                                                 range->maxValue, range->value, paint);
        const UILogicalCornerRadii trackRadii =
            UILogicalCornerRadii::uniform(paint.trackThickness * 0.5F);
        const UILogicalCornerRadii thumbRadii =
            UILogicalCornerRadii::uniform(paint.thumbExtent * 0.5F);
        add(geometry.track, controlColor(paint.trackColor), trackRadii);
        if (geometry.fraction > 0.0F)
        {
            add(geometry.filledTrack, controlColor(paint.filledTrackColor), trackRadii);
        }
        const UIStraightSrgba8Color thumbColor =
            armedSlider == layoutEntry.node && paint.draggingThumbColor.alpha != 0
                ? paint.draggingThumbColor
                : isFocusVisible(layoutEntry.node) && paint.focusedThumbColor.alpha != 0
                    ? paint.focusedThumbColor
                    : hoveredPrimaryControl == layoutEntry.node &&
                              paint.hoveredThumbColor.alpha != 0
                        ? paint.hoveredThumbColor
                    : paint.thumbColor;
        add(geometry.thumb, controlColor(thumbColor), thumbRadii);
    } else if (record != nullptr && record->kind == BuiltinElementKind::Splitter &&
               splitViewStorage.containsSplitter(layoutEntry.node))
    {
        const UISplitterPaint& paint = splitViewStorage.splitterPaintByIndex(nodeIndex);
        const UINodeId splitView = splitViewStorage.splitViewForSplitter(layoutEntry.node);
        const SplitViewState* splitState = splitViewStorage.trySplitView(splitView);
        if (splitState != nullptr)
        {
            const bool horizontal =
                splitState->config.orientation == UISplitViewOrientation::Horizontal;
            const auto centeredLine = [&](float thickness) noexcept {
                const float resolvedThickness = (std::min)(
                    thickness, horizontal ? layoutEntry.worldRect.width
                                          : layoutEntry.worldRect.height);
                return horizontal
                           ? UILogicalRect{
                                 .x = normalizeFloat(layoutEntry.worldRect.x +
                                                     (layoutEntry.worldRect.width - resolvedThickness) * 0.5F),
                                 .y = layoutEntry.worldRect.y,
                                 .width = normalizeFloat(resolvedThickness),
                                 .height = layoutEntry.worldRect.height,
                             }
                           : UILogicalRect{
                                 .x = layoutEntry.worldRect.x,
                                 .y = normalizeFloat(layoutEntry.worldRect.y +
                                                     (layoutEntry.worldRect.height - resolvedThickness) * 0.5F),
                                 .width = layoutEntry.worldRect.width,
                                 .height = normalizeFloat(resolvedThickness),
                             };
            };
            if (isFocusVisible(layoutEntry.node) && paint.focusRingColor.alpha != 0)
            {
                add(centeredLine(paint.focusRingThickness),
                    controlColor(paint.focusRingColor));
            }
            UIStraightSrgba8Color lineColor = paint.lineColor;
            if (isCandidateNodeEnabled(layoutEntry.node))
            {
                if (armedSlider == layoutEntry.node && paint.draggingLineColor.alpha != 0)
                {
                    lineColor = paint.draggingLineColor;
                } else if (hoveredPrimaryControl == layoutEntry.node &&
                           paint.hoveredLineColor.alpha != 0)
                {
                    lineColor = paint.hoveredLineColor;
                }
            }
            add(centeredLine(paint.lineThickness), controlColor(lineColor));
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::ScrollView &&
               nodeIndex < scrollViewPaintsByNodeIndex.size() &&
               nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
    {
        const UIScrollViewPaint& scroll = scrollViewPaintsByNodeIndex[nodeIndex];
        const ScrollViewLayoutScratch& scrollLayout = scrollViewLayoutScratchByNodeIndex[nodeIndex];
        const auto addBar = [&](UIScrollAxes axis) noexcept {
            const ScrollBarGeometry geometry =
                makeScrollBarGeometry(scrollLayout.metrics, scrollLayout.viewportRect, scroll, axis);
            if (!geometry.visible)
            {
                return;
            }
            add(geometry.track, controlColor(scroll.trackColor));
            const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                             armedScrollAxis == axis &&
                                                             scroll.draggingThumbColor.alpha != 0
                                                         ? scroll.draggingThumbColor
                                                         : scroll.thumbColor;
            add(geometry.thumb, controlColor(thumbColor));
        };
        addBar(UIScrollAxes::Horizontal);
        addBar(UIScrollAxes::Vertical);
    } else if (record != nullptr && record->kind == BuiltinElementKind::ListView &&
               nodeIndex < listViewStatesByNodeIndex.size() && nodeIndex < listViewLayoutScratchByNodeIndex.size())
    {
        const ListViewState& list = listViewStatesByNodeIndex[nodeIndex];
        const ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[nodeIndex];
        const ScrollBarGeometry geometry =
            makeListViewScrollBarGeometry(listLayout.metrics, listLayout.viewportRect, list.paint.scrollBar);
        if (geometry.visible)
        {
            add(geometry.track, controlColor(list.paint.scrollBar.trackColor));
            const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                             list.paint.scrollBar.draggingThumbColor.alpha != 0
                                                         ? list.paint.scrollBar.draggingThumbColor
                                                         : list.paint.scrollBar.thumbColor;
            add(geometry.thumb, controlColor(thumbColor));
        }
    } else if (record != nullptr &&
               record->kind == BuiltinElementKind::VirtualGridView)
    {
        const VirtualGridViewState* state =
            virtualGridViewStorage.tryView(layoutEntry.node);
        const VirtualGridViewLayoutScratch* gridLayout =
            virtualGridViewStorage.tryLayoutScratch(layoutEntry.node);
        if (state != nullptr && gridLayout != nullptr)
        {
            const ScrollBarGeometry geometry =
                makeVirtualGridViewScrollBarGeometry(
                    gridLayout->metrics, gridLayout->viewportRect,
                    state->paint.scrollBar);
            if (geometry.visible)
            {
                add(geometry.track,
                    controlColor(state->paint.scrollBar.trackColor));
                const UIStraightSrgba8Color thumbColor =
                    scrollThumbDragActive &&
                            armedScrollView == layoutEntry.node &&
                            state->paint.scrollBar.draggingThumbColor.alpha != 0
                        ? state->paint.scrollBar.draggingThumbColor
                        : state->paint.scrollBar.thumbColor;
                add(geometry.thumb, controlColor(thumbColor));
            }
        }
    } else if (record != nullptr &&
               record->kind == BuiltinElementKind::DataGrid)
    {
        const DataGridState* state =
            dataGridStorage.tryGrid(layoutEntry.node);
        const DataGridLayoutScratch* gridLayout =
            dataGridStorage.tryLayoutScratch(layoutEntry.node);
        if (state != nullptr && gridLayout != nullptr)
        {
            const auto geometry = makeDataGridScrollBarGeometry(
                gridLayout->metrics, gridLayout->bodyViewportRect,
                state->paint.scrollBar);
            const auto addBar = [&](UIScrollAxes axis,
                                    const ScrollBarGeometry& bar) noexcept {
                if (!bar.visible)
                {
                    return;
                }
                add(bar.track,
                    controlColor(state->paint.scrollBar.trackColor));
                const UIStraightSrgba8Color thumbColor =
                    scrollThumbDragActive &&
                            armedScrollView == layoutEntry.node &&
                            armedScrollAxis == axis &&
                            state->paint.scrollBar.draggingThumbColor.alpha != 0
                        ? state->paint.scrollBar.draggingThumbColor
                        : state->paint.scrollBar.thumbColor;
                add(bar.thumb, controlColor(thumbColor));
            };
            addBar(UIScrollAxes::Horizontal, geometry.horizontal);
            addBar(UIScrollAxes::Vertical, geometry.vertical);
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::TreeView &&
               nodeIndex < treeViewStatesByNodeIndex.size() && nodeIndex < treeViewLayoutScratchByNodeIndex.size())
    {
        const TreeViewState& tree = treeViewStatesByNodeIndex[nodeIndex];
        const TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[nodeIndex];
        const ScrollBarGeometry geometry =
            makeTreeViewScrollBarGeometry(treeLayout.metrics, treeLayout.viewportRect, tree.paint.scrollBar);
        if (geometry.visible)
        {
            add(geometry.track, controlColor(tree.paint.scrollBar.trackColor));
            const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                             tree.paint.scrollBar.draggingThumbColor.alpha != 0
                                                         ? tree.paint.scrollBar.draggingThumbColor
                                                         : tree.paint.scrollBar.thumbColor;
            add(geometry.thumb, controlColor(thumbColor));
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::Dropdown &&
               nodeIndex < dropdownStatesByNodeIndex.size())
    {
        const UIDropdownPaint& paint = dropdownStatesByNodeIndex[nodeIndex].paint;
        if (paint.indicatorWidth > 0.0F && paint.indicatorHeight > 0.0F)
        {
            constexpr usize StripeCount = 3;
            const float stripeHeight = paint.indicatorHeight / static_cast<float>(StripeCount);
            const float centerX =
                layoutEntry.worldRect.right() - paint.indicatorInset - paint.indicatorWidth * 0.5F;
            const float top =
                layoutEntry.worldRect.y + (layoutEntry.worldRect.height - paint.indicatorHeight) * 0.5F;
            const UIPremultipliedRgba8Color color = controlColor(paint.indicatorColor);
            for (usize stripe = 0; stripe < StripeCount; ++stripe)
            {
                const float width = paint.indicatorWidth * static_cast<float>(StripeCount - stripe) /
                                    static_cast<float>(StripeCount);
                add(
                    UILogicalRect{
                        .x = normalizeFloat(centerX - width * 0.5F),
                        .y = normalizeFloat(top + stripeHeight * static_cast<float>(stripe)),
                        .width = normalizeFloat(width),
                        .height = normalizeFloat(stripeHeight),
                    },
                    color);
            }
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::MenuItem)
    {
        const MenuItemState* item = menuStorage.tryItem(layoutEntry.node);
        if (item != nullptr && item->config.kind == UIMenuItemKind::Separator)
        {
            const float thickness = (std::min)(
                     productTheme.controls.menuSeparatorThickness,
                layoutEntry.contentPlacement.contentBox.height);
            if (thickness > 0.0F)
            {
                add(
                    UILogicalRect{
                        .x = layoutEntry.contentPlacement.contentBox.x,
                        .y = normalizeFloat(
                            layoutEntry.contentPlacement.contentBox.y +
                            (layoutEntry.contentPlacement.contentBox.height - thickness) * 0.5F),
                        .width = layoutEntry.contentPlacement.contentBox.width,
                        .height = thickness,
                    },
                    controlColor(productTheme.colors.outline));
            }
        } else if (item != nullptr && item->checked &&
                   (item->config.kind == UIMenuItemKind::Check ||
                    item->config.kind == UIMenuItemKind::Radio))
        {
            const float outerExtent = (std::min)(
                productTheme.controls.menuItemIndicatorExtent,
                layoutEntry.contentPlacement.contentBox.height);
            const float inset = item->config.kind == UIMenuItemKind::Radio
                                    ? outerExtent * 0.25F
                                    : 0.0F;
            const float extent = (std::max)(0.0F, outerExtent - inset * 2.0F);
            add(
                UILogicalRect{
                    .x = normalizeFloat(
                        layoutEntry.contentPlacement.contentBox.x + inset),
                    .y = normalizeFloat(
                        layoutEntry.contentPlacement.contentBox.y +
                        (layoutEntry.contentPlacement.contentBox.height - outerExtent) * 0.5F + inset),
                    .width = extent,
                    .height = extent,
                },
                controlColor(productTheme.colors.primary));
        } else if (item != nullptr &&
                   item->config.kind == UIMenuItemKind::Submenu)
        {
            constexpr usize StripeCount = 3;
            const UILayoutStyle itemLayout =
                resolvedLayoutStyle(nodeIndex);
            const float extent = (std::min)(
                productTheme.controls.menuItemIndicatorExtent,
                layoutEntry.worldRect.height);
            const float stripeExtent = extent /
                                       static_cast<float>(StripeCount);
            const float left = layoutEntry.worldRect.right() -
                               itemLayout.padding.right - extent;
            const float top = layoutEntry.worldRect.y +
                              (layoutEntry.worldRect.height - extent) * 0.5F;
            const UIPremultipliedRgba8Color color =
                controlColor(productTheme.colors.onSurface);
            for (usize stripe = 0; stripe < StripeCount; ++stripe)
            {
                const float offset = stripe == 1 ? stripeExtent : 0.0F;
                add(UILogicalRect{
                        .x = normalizeFloat(left + offset),
                        .y = normalizeFloat(
                            top + stripeExtent * static_cast<float>(stripe)),
                        .width = stripeExtent,
                        .height = stripeExtent,
                    },
                    color);
            }
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::DropdownItem)
    {
        add(layoutEntry.worldRect, resolvedDropdownSelectionColor(layoutEntry.node));
    } else if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem)
    {
        add(layoutEntry.worldRect, resolvedListViewSelectionColor(layoutEntry.node));
    } else if (record != nullptr &&
               record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        add(layoutEntry.worldRect,
            resolvedVirtualGridViewSelectionColor(layoutEntry.node));
    } else if (record != nullptr &&
               record->kind == BuiltinElementKind::DataGridColumnHeader)
    {
        const DataGridColumnState* column =
            dataGridStorage.tryColumn(layoutEntry.node);
        const DataGridState* state = column != nullptr
                                         ? dataGridStorage.tryGrid(
                                               column->dataGrid)
                                         : nullptr;
        if (column != nullptr && column->bound && state != nullptr)
        {
            if (state->paint.columnHeaderBackgroundColor.alpha != 0)
            {
                add(layoutEntry.worldRect,
                    controlColor(
                        state->paint.columnHeaderBackgroundColor));
            }
            addGridLines(layoutEntry.worldRect,
                         state->paint.gridLineColor);
        }
    } else if (record != nullptr &&
               record->kind == BuiltinElementKind::DataGridCell)
    {
        const DataGridCellState* cell =
            dataGridStorage.tryCell(layoutEntry.node);
        const DataGridState* state = cell != nullptr
                                         ? dataGridStorage.tryGrid(
                                               cell->dataGrid)
                                         : nullptr;
        if (cell != nullptr && cell->bound && state != nullptr)
        {
            const UIPremultipliedRgba8Color selection =
                resolvedDataGridRowSelectionColor(layoutEntry.node);
            if (!selection.isTransparent())
            {
                add(layoutEntry.worldRect, selection);
            }
            addGridLines(layoutEntry.worldRect,
                         state->paint.gridLineColor);
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem &&
               nodeIndex < treeViewItemStatesByNodeIndex.size())
    {
        add(layoutEntry.worldRect, resolvedTreeViewSelectionColor(layoutEntry.node));
        const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
        const UINodeId treeView = treeViewForItem(layoutEntry.node);
        const UIPremultipliedRgba8Color disclosure = resolvedTreeViewDisclosureColor(layoutEntry.node);
        if (treeView.hasValue() && !disclosure.isTransparent())
        {
            const UITreeViewStyle& style = treeViewStatesByNodeIndex[treeView.index()].style;
            const UILogicalRect disclosureRect =
                makeTreeViewDisclosureRect(layoutEntry.worldRect, style, item.level);
            const float stroke = normalizeFloat((std::max)(1.0F, disclosureRect.height / 6.0F));
            add(
                UILogicalRect{
                    .x = disclosureRect.x,
                    .y = normalizeFloat(disclosureRect.y + (disclosureRect.height - stroke) * 0.5F),
                    .width = disclosureRect.width,
                    .height = stroke,
                },
                disclosure);
            if (!item.expanded)
            {
                add(
                    UILogicalRect{
                        .x = normalizeFloat(disclosureRect.x + (disclosureRect.width - stroke) * 0.5F),
                        .y = disclosureRect.y,
                        .width = stroke,
                        .height = disclosureRect.height,
                    },
                    disclosure);
            }
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::ProgressBar &&
               nodeIndex < progressBarStatesByNodeIndex.size())
    {
        const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
        const float fraction = normalizedRangeFraction(progress.value, progress.minValue, progress.maxValue);
        if (fraction > 0.0F)
        {
            add(
                UILogicalRect{
                    .x = layoutEntry.worldRect.x,
                    .y = layoutEntry.worldRect.y,
                    .width = normalizeFloat(layoutEntry.worldRect.width * fraction),
                    .height = layoutEntry.worldRect.height,
                },
                controlColor(progress.paint.fillColor));
        }
    } else if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
               nodeIndex < radioButtonStatesByNodeIndex.size() &&
               radioButtonStatesByNodeIndex[nodeIndex].paint.indicatorVisible)
    {
        const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
        const float extent = (std::min)({
            radio.paint.indicatorExtent,
            layoutEntry.worldRect.width,
            layoutEntry.worldRect.height,
        });
        const UILogicalRect indicatorRect{
            .x = layoutEntry.worldRect.x,
            .y = normalizeFloat(layoutEntry.worldRect.y +
                                (layoutEntry.worldRect.height - extent) * 0.5F),
            .width = normalizeFloat(extent),
            .height = normalizeFloat(extent),
        };
        add(indicatorRect, resolvedRadioIndicatorColor(layoutEntry.node, nodeIndex),
            UILogicalCornerRadii::uniform(extent * 0.5F));
        if (radio.selected)
        {
            const float inset = radio.paint.selectedIndicatorInset;
            add(
                UILogicalRect{
                    .x = normalizeFloat(indicatorRect.x + inset),
                    .y = normalizeFloat(indicatorRect.y + inset),
                    .width = normalizeFloat(extent - inset * 2.0F),
                    .height = normalizeFloat(extent - inset * 2.0F),
                },
                controlColor(radio.paint.selectedIndicatorColor),
                UILogicalCornerRadii::uniform((extent - inset * 2.0F) * 0.5F));
        }
    }

    if (batchCapacityExceeded)
    {
        return fail(Core::CoreErrorCode::Internal, "UI control paint primitive batch capacity has been exhausted");
    }
    return batch;
}

[[nodiscard]] Detail::UITextEditPaintState UIContext::Impl::resolveTextEditPaintState(
    UINodeId node, bool applyDisabledOpacity, bool useCandidateVisualState) const noexcept
{
    const u32 nodeIndex = node.index();
    const NodeRecord* record = recordByIndex(nodeIndex);
    const bool focused = record != nullptr && record->kind == BuiltinElementKind::TextEdit &&
                         isNodeEnabled(node) && textInputFocus == node && isLiveTextEdit(textInputFocus);
    const WidgetTextState* textState =
        nodeIndex < textStatesByIndex.size() ? &textStatesByIndex[nodeIndex] : nullptr;
    const UITextStyle style = textState != nullptr ? textState->style : UITextStyle{};
    UIPremultipliedRgba8Color textColor{};
    if (textState != nullptr)
    {
        const auto motionPresentation = motionPresentationFor(node);
        if (motionPresentation.hasTextColor)
        {
            textColor = premultiply(motionPresentation.textColor);
        }
        else if (applyDisabledOpacity && nodeIndex < localTextColorCacheByIndex.size())
        {
            textColor = widgetPaintColor(node, localTextColorCacheByIndex[nodeIndex]);
        }
        else
        {
            textColor = premultiply(style.color);
        }
        if (motionPresentation.hasOpacity ||
            (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
             presentationOpacityValidByNodeIndex[nodeIndex] != 0))
        {
            const float opacity = currentOpacity(node, nodeIndex);
            if (opacity < 1.0F)
            {
                textColor = applyOpacity(
                    textColor,
                    static_cast<u8>(std::clamp(opacity, 0.0F, 1.0F) * 255.0F + 0.5F));
            }
        }
    }
    const bool preeditActive = focused && imeComposition.active();
    const bool multilineEnabled = nodeIndex < textEditMultilineByNodeIndex.size() &&
                                  textEditMultilineByNodeIndex[nodeIndex].enabled;
    const UITextEditPaint paint =
        nodeIndex < textEditPaintsByNodeIndex.size() ? textEditPaintsByNodeIndex[nodeIndex]
                                                     : UITextEditPaint{};
    const Detail::UITextInputState* textInputState = behaviorStateStorage.tryTextInputState(nodeIndex);
    const auto& visualLinesByNodeIndex = useCandidateVisualState
                                             ? candidateTextEditVisualLinesByNodeIndex
                                             : textEditVisualLinesByNodeIndex;
    const auto& visualLayoutsByNodeIndex = useCandidateVisualState
                                               ? candidateTextEditVisualLayoutsByNodeIndex
                                               : textEditVisualLayoutsByNodeIndex;
    const auto& scrollYByNodeIndex = useCandidateVisualState
                                         ? candidateTextEditScrollYByNodeIndex
                                         : textEditScrollYByNodeIndex;
    return Detail::UITextEditPaintState{
        .focused = focused,
        .preeditActive = preeditActive,
        .committedText = presentationTextViewFor(nodeIndex),
        .selection = focused && textInputState != nullptr ? textInputState->selection : UITextSelection{},
        .caretAffinity = nodeIndex < textEditCaretAffinityByNodeIndex.size()
                             ? textEditCaretAffinityByNodeIndex[nodeIndex]
                             : Detail::UITextEditCaretAffinity::Downstream,
        .preeditText = preeditActive ? imeComposition.preeditUtf8() : std::string_view{},
        .preeditCursorCodepoint = preeditActive ? imeComposition.cursorCodepoint() : 0,
        .style = style,
        .textColor = textColor,
        .selectionColor = premultiply(paint.selectionBackgroundColor),
        .caretColor = premultiply(paint.caretColor),
        .rasterSource =
            Detail::UITextPaintRasterSource{
                .rasterizer = textRasterizer.get(),
                .face = textFace,
                .atlas = glyphAtlas.get(),
            },
        .overflow = textState != nullptr ? textState->overflow : UITextOverflow::Clip,
        .textWrapMode = textState != nullptr
                            ? textState->wrapMode
                            : UITextWrapMode::NoWrap,
        .multilineEnabled = multilineEnabled,
        .wrapMode = nodeIndex < textEditMultilineByNodeIndex.size()
                        ? textEditMultilineByNodeIndex[nodeIndex].wrapMode
                        : UITextEditWrapMode::NoWrap,
        .visualLines = multilineEnabled && nodeIndex < visualLinesByNodeIndex.size()
                           ? std::span<const Detail::UITextEditVisualLine>(visualLinesByNodeIndex[nodeIndex])
                           : std::span<const Detail::UITextEditVisualLine>{},
        .visualLayout = multilineEnabled && nodeIndex < visualLayoutsByNodeIndex.size()
                            ? visualLayoutsByNodeIndex[nodeIndex]
                            : Detail::UITextEditVisualLayout{},
        .scrollY = nodeIndex < scrollYByNodeIndex.size()
                       ? scrollYByNodeIndex[nodeIndex] : 0.0F,
    };
}

// Both the counting and the appending paint pass must observe the same
// available width, so the content box is injected here instead of at each
// call site. ADR 0022: truncation reads the one committed content
// placement and never re-derives geometry from worldRect + padding.
[[nodiscard]] Detail::UITextEditPaintState UIContext::Impl::resolveTextEditPaintStateFor(
    const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity,
    bool useCandidateVisualState) const noexcept
{
    Detail::UITextEditPaintState state = resolveTextEditPaintState(
        layoutEntry.node, applyDisabledOpacity, useCandidateVisualState);
    state.availableWidth = layoutEntry.contentPlacement.contentBox.width;
    state.intrinsicWidth = layoutEntry.contentPlacement.hasIntrinsicContent
                               ? layoutEntry.contentPlacement.intrinsicSize.width
                               : 0.0F;
    return state;
}

[[nodiscard]] Core::Result<usize> UIContext::Impl::countPaintEntries(
    const UICommittedLayoutEntry& layoutEntry, bool useCandidateTextEditVisualState) const
{
    usize paintEntryCount = 0;
    if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
    {
        return paintEntryCount;
    }
    if (!contains(layoutEntry.node))
    {
        return fail(UIErrorCode::InvalidNode, "UI paint snapshot layout references a stale node");
    }

    const u32 nodeIndex = layoutEntry.node.index();
    const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
    const UIPremultipliedRgba8Color resolvedFill =
        presentationBoxFill(layoutEntry.node, nodeIndex);
    const UILogicalRect paintWorld =
        presentationPaintWorldRect(layoutEntry.node, nodeIndex, layoutEntry.worldRect);
    paintEntryCount += countBoxChromePaintEntries(boxPaint, paintWorld, !resolvedFill.isTransparent());
    paintEntryCount += countCanvasPaintEntries(layoutEntry);
    if (const UIImageContent* image = imageContentStorage.get(nodeIndex); image != nullptr)
    {
        const UIStraightSrgba8Color tint = resolvedImageTintColor(nodeIndex, *image);
        if (tint.alpha != 0 && layoutEntry.contentPlacement.contentBox.width > 0.0F &&
            layoutEntry.contentPlacement.contentBox.height > 0.0F)
        {
            ++paintEntryCount;
        }
    }
    auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, false);
    if (!controlPaintBatch)
    {
        return Core::failure(controlPaintBatch.error());
    }
    paintEntryCount += controlPaintBatch->size();
    UICommittedLayoutEntry textLayoutEntry = layoutEntry;
    textLayoutEntry.contentPlacement = virtualGridTextPlacement(layoutEntry);
    paintEntryCount += Detail::UITextEditPaintEmitter::countEntries(
        resolveTextEditPaintStateFor(
            textLayoutEntry, false, useCandidateTextEditVisualState));
    return paintEntryCount;
}

void UIContext::Impl::refreshLocalPaintCache(u32 nodeIndex) noexcept
{
    const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
    localSolidFillCacheByIndex[nodeIndex] =
        paint.solidFill.has_value() ? premultiply(paint.solidFill->color) : UIPremultipliedRgba8Color{};
    localTextColorCacheByIndex[nodeIndex] =
        nodeIndex < textStatesByIndex.size() && textStatesByIndex[nodeIndex].hasContent
            ? premultiply(textStatesByIndex[nodeIndex].style.color)
            : UIPremultipliedRgba8Color{};
}

[[nodiscard]] UIContext::Impl::PaintCacheRebuildStatistics
UIContext::Impl::rebuildDirtyPaintCaches(
    std::span<const UICommittedLayoutEntry> layoutEntries,
    StyleInteractionNodeSet& interactionCandidates) noexcept
{
    PaintCacheRebuildStatistics statistics{};
    interactionCandidates = committedStyleInteractionNodes;
    interactionCandidates.merge(currentStyleInteractionNodes());
    const auto rebuildNode = [this, &statistics](u32 nodeIndex) noexcept {
        ++statistics.styleInspectedNodeCount;
        refreshLocalPaintCache(nodeIndex);
        statistics.styleCandidateRuleCount += refreshResolvedStyleCache(nodeIndex);
        ++statistics.paintCacheRebuildCount;
        ++statistics.styleResolvedNodeCount;
    };
    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        const u32 nodeIndex = layoutEntry.node.index();
        if (nodeIndex >= dirtyQueueStorage.nodeCapacity() ||
            nodeIndex >= styleStatesByNodeIndex.size())
        {
            continue;
        }

        const NodeRecord* record = recordByIndex(nodeIndex);
        const UIDirty dirty = dirtyQueueStorage.flags(nodeIndex);
        const bool paintDirty = hasDirty(dirty, UIDirty::Paint);
        const bool virtualCollectionOwner =
            record != nullptr &&
            (record->kind == BuiltinElementKind::ListView ||
             record->kind == BuiltinElementKind::TreeView ||
             record->kind == BuiltinElementKind::VirtualGridView ||
             record->kind == BuiltinElementKind::DataGrid);
        if (paintDirty)
        {
            rebuildNode(nodeIndex);
        }
        if (!virtualCollectionOwner ||
            !hasDirty(dirty, UIDirty::Style | UIDirty::Paint))
        {
            continue;
        }

        // Virtual collection selection and row rebinding dirty the owner.
        // Refresh its bounded materialized row pool without turning every
        // paint-only state update into a full-tree style scan.
        for (u32 childIndex = record->firstChildIndex;
             childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            const bool materializedRow =
                child->kind == BuiltinElementKind::ListViewItem ||
                child->kind == BuiltinElementKind::TreeViewItem ||
                child->kind == BuiltinElementKind::VirtualGridViewItem;
            const bool childAlreadyDirty =
                childIndex < dirtyQueueStorage.nodeCapacity() &&
                hasDirty(dirtyQueueStorage.flags(childIndex), UIDirty::Paint);
            if (materializedRow && !childAlreadyDirty)
            {
                rebuildNode(childIndex);
            }
            childIndex = nextSiblingIndex;
        }
    }

    for (usize index = 0; index < interactionCandidates.count; ++index)
    {
        const UINodeId node = interactionCandidates.nodes[index];
        if (!contains(node) || node.index() >= styleStatesByNodeIndex.size())
        {
            continue;
        }
        const UIStyleState states = deriveStyleState(node, node.index());
        if (styleStatesByNodeIndex[node.index()] != states)
        {
            rebuildNode(node.index());
        }
    }
    return statistics;
}

void UIContext::Impl::appendTextGlyphPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                           const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                           bool useCandidateTextEditVisualState) noexcept
{
    UICommittedLayoutEntry textLayoutEntry = layoutEntry;
    textLayoutEntry.contentPlacement = virtualGridTextPlacement(layoutEntry);
    const auto caretGeometry = Detail::UITextEditPaintEmitter::append(
        output, textLayoutEntry, nextPaintOrdinal,
        resolveTextEditPaintStateFor(
            textLayoutEntry, true, useCandidateTextEditVisualState));
    if (caretGeometry.has_value() && caretGeometry->effectiveClip.width > 0.0F &&
        caretGeometry->effectiveClip.height > 0.0F &&
        caretGeometry->worldRect.x < caretGeometry->effectiveClip.right() &&
        caretGeometry->worldRect.right() > caretGeometry->effectiveClip.x &&
        caretGeometry->worldRect.y < caretGeometry->effectiveClip.bottom() &&
        caretGeometry->worldRect.bottom() > caretGeometry->effectiveClip.y)
    {
        candidateTextInputCaretRect = caretGeometry->worldRect;
    }
}

[[nodiscard]] UILogicalRect UIContext::Impl::resolveImageDestination(
    const UICommittedContentPlacement& placement, const UIImageContent& image) noexcept
{
    const UILogicalRect box = placement.contentBox;
    const UILogicalSize intrinsic = image.source.intrinsicLogicalSize;
    if (box.width <= 0.0F || box.height <= 0.0F)
    {
        return {};
    }
    if (image.fit == UIImageFit::Fill)
    {
        return box;
    }

    float scale = 1.0F;
    if (image.fit == UIImageFit::Contain || image.fit == UIImageFit::Cover)
    {
        const float horizontalScale = box.width / intrinsic.width;
        const float verticalScale = box.height / intrinsic.height;
        scale = image.fit == UIImageFit::Contain
                    ? (std::min)(horizontalScale, verticalScale)
                    : (std::max)(horizontalScale, verticalScale);
    }
    const float width = normalizeFloat(intrinsic.width * scale);
    const float height = normalizeFloat(intrinsic.height * scale);
    const auto alignedOffset = [](UIAxisAlignment axis, float freeSpace) noexcept {
        switch (axis)
        {
        case UIAxisAlignment::Center:
            return freeSpace * 0.5F;
        case UIAxisAlignment::End:
            return freeSpace;
        case UIAxisAlignment::Start:
        case UIAxisAlignment::Stretch:
            return 0.0F;
        }
        return 0.0F;
    };
    return UILogicalRect{
        .x = normalizeFloat(box.x + alignedOffset(image.alignment.horizontal, box.width - width)),
        .y = normalizeFloat(box.y + alignedOffset(image.alignment.vertical, box.height - height)),
        .width = width,
        .height = height,
    };
}

void UIContext::Impl::appendImagePaint(std::pmr::vector<UICommittedPaintEntry>& output,
                      const UICommittedLayoutEntry& layoutEntry,
                      u32& nextPaintOrdinal) const noexcept
{
    const u32 nodeIndex = layoutEntry.node.index();
    const UIImageContent* image = imageContentStorage.get(nodeIndex);
    const NodeRecord* record = recordByIndex(nodeIndex);
    if (image == nullptr || record == nullptr)
    {
        return;
    }
    const UIStraightSrgba8Color tint = resolvedImageTintColor(nodeIndex, *image);
    if (tint.alpha == 0)
    {
        return;
    }
    const UICommittedContentPlacement imagePlacement =
        virtualGridImagePlacement(layoutEntry);
    const UILogicalRect destination = resolveImageDestination(imagePlacement, *image);
    if (destination.width <= 0.0F || destination.height <= 0.0F)
    {
        return;
    }
    output.push_back(UICommittedPaintEntry{
        .node = layoutEntry.node,
        .root = idForIndex(record->rootIndex),
        .worldRect = destination,
        .effectiveClip = intersectRects(layoutEntry.effectiveClip,
                                        imagePlacement.contentBox),
        .paintOrdinal = nextPaintOrdinal,
        .solidFill = premultiply(tint),
        .kind = UICommittedPaintKind::Image,
        .imageSource = image->source,
        .imageSampling = image->sampling,
    });
    ++nextPaintOrdinal;
}

[[nodiscard]] Core::Status UIContext::Impl::appendPaintEntries(std::pmr::vector<UICommittedPaintEntry>& output,
                                              const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                                              bool useCandidateTextEditVisualState)
{
    const u32 nodeIndex = layoutEntry.node.index();
    const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
    const UIPremultipliedRgba8Color fill =
        presentationBoxFill(layoutEntry.node, nodeIndex);
    const UILogicalRect paintWorld =
        presentationPaintWorldRect(layoutEntry.node, nodeIndex, layoutEntry.worldRect);
    appendBoxChromePaints(output, layoutEntry.node, paintWorld, layoutEntry.effectiveClip,
                          nextPaintOrdinal, boxPaint, fill);
    appendCanvasPaints(output, layoutEntry, nextPaintOrdinal);
    auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, true);
    if (!controlPaintBatch)
    {
        return Core::failure(controlPaintBatch.error());
    }
    const NodeRecord* record = recordByIndex(nodeIndex);
    const bool virtualGridItem =
        record != nullptr &&
        record->kind == BuiltinElementKind::VirtualGridViewItem;
    if (virtualGridItem)
    {
        // The item batch contains the opaque selected-card background. It
        // must sit behind the thumbnail instead of covering it.
        controlPaintBatch->appendTo(
            output, layoutEntry.node, layoutEntry.effectiveClip,
            nextPaintOrdinal);
        appendImagePaint(output, layoutEntry, nextPaintOrdinal);
    }
    else
    {
        appendImagePaint(output, layoutEntry, nextPaintOrdinal);
        controlPaintBatch->appendTo(
            output, layoutEntry.node, layoutEntry.effectiveClip,
            nextPaintOrdinal);
    }
    appendTextGlyphPaints(
        output, layoutEntry, nextPaintOrdinal, useCandidateTextEditVisualState);
    return Core::success();
}

[[nodiscard]] constexpr Detail::UIPaintSnapshotSourceAdapter UIContext::Impl::paintSnapshotSourceAdapter() noexcept
{
    return Detail::UIPaintSnapshotSourceAdapter{
        .countEntries = [](const void* context, const UICommittedLayoutEntry& layoutEntry) -> Core::Result<usize> {
            const auto& source = *static_cast<const PaintSnapshotSourceContext*>(context);
            return source.impl->countPaintEntries(
                layoutEntry, source.useCandidateTextEditVisualState);
        },
        .appendEntries = [](void* context, std::pmr::vector<UICommittedPaintEntry>& output,
                            const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) -> Core::Status {
            auto& source = *static_cast<PaintSnapshotSourceContext*>(context);
            return source.impl->appendPaintEntries(
                output, layoutEntry, nextPaintOrdinal,
                source.useCandidateTextEditVisualState);
        },
    };
}

[[nodiscard]] Core::Result<usize>
UIContext::Impl::validatePaintCandidateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries,
                               bool useCandidateTextEditVisualState)
{
    PaintSnapshotSourceContext source{
        .impl = this,
        .useCandidateTextEditVisualState = useCandidateTextEditVisualState,
    };
    return paintSnapshotBuilder.validateCapacity(
        layoutEntries, &source, paintSnapshotSourceAdapter());
}

[[nodiscard]] Core::Status UIContext::Impl::buildCommittedPaint(std::pmr::vector<UICommittedPaintEntry>& output,
                                               std::span<const UICommittedLayoutEntry> layoutEntries,
                                               bool useCandidateTextEditVisualState)
{
    candidateTextInputCaretRect.reset();
    PaintSnapshotSourceContext source{
        .impl = this,
        .useCandidateTextEditVisualState = useCandidateTextEditVisualState,
    };
    return paintSnapshotBuilder.build(
        output, layoutEntries, &source, paintSnapshotSourceAdapter());
}

} // namespace Tina::UI
