#include "EditorWorkspaceUiRecipes.hpp"

#include <tina/ui/UIElement.hpp>

#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] UI::UILayoutStyle iconButtonLayout(
    const UI::UITheme& theme, UI::UILayoutStyle layout) noexcept
{
    if (layout.size.width.isAuto()) {
        layout.size.width =
            UI::UILayoutLength::Px(theme.controls.iconButtonExtent);
    }
    if (layout.size.height.isAuto()) {
        layout.size.height =
            UI::UILayoutLength::Px(theme.controls.iconButtonExtent);
    }
    layout.flexContainer.justifyContent = UI::UIJustifyContent::Center;
    layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    return layout;
}

[[nodiscard]] UI::UILayoutStyle iconLayout(const UI::UITheme& theme) noexcept
{
    UI::UILayoutStyle layout{};
    layout.size.width = UI::UILayoutLength::Px(theme.controls.iconExtent);
    layout.size.height = UI::UILayoutLength::Px(theme.controls.iconExtent);
    layout.flexItem.alignSelf = UI::UIAlignSelf::Center;
    return layout;
}

[[nodiscard]] UI::UIImageContent editorIconImage(EditorIcon icon) noexcept
{
    const UI::UIIconContent content = editorIconContent(icon);
    return UI::UIImageContent{
        .source = content.source,
        .fit = UI::UIImageFit::Contain,
        .alignment = content.alignment,
        .tint = content.tint,
        .sampling = content.sampling,
    };
}

} // namespace

Core::Result<EditorNumberText> formatEditorNumber(float value) noexcept
{
    if (!std::isfinite(value)) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor UI number must be finite");
    }

    EditorNumberText text{};
    const float normalized = value == 0.0F ? 0.0F : value;
    const std::to_chars_result result = std::to_chars(
        text.bytes.data(), text.bytes.data() + text.bytes.size() - 1U,
        normalized, std::chars_format::general, 6);
    if (result.ec != std::errc{}) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Editor UI number exceeds its fixed text capacity");
    }
    text.size = static_cast<Core::usize>(result.ptr - text.bytes.data());
    text.bytes[text.size] = '\0';
    return text;
}

UI::UILayoutStyle editorDocumentTabLayout(
    const UI::UITheme& theme, UI::UIVisibility visibility) noexcept
{
    UI::UILayoutStyle layout{};
    layout.size.width = UI::UILayoutLength::Px(170.0F);
    layout.size.height = UI::UILayoutLength::Px(theme.controls.tabHeight);
    layout.minMax.minWidth = UI::UILayoutLength::Px(112.0F);
    layout.flexItem.grow = 0.0F;
    layout.flexItem.shrink = 1.0F;
    layout.visibility = visibility;
    return layout;
}

UI::UITextStyle makeEditorInfoTextStyle(
    const UI::UITheme& theme, float logicalSize) noexcept
{
    UI::UITextStyle style = UI::makeBodyTextStyle(theme, logicalSize);
    style.color = theme.colors.primary;
    return style;
}

Core::Result<UI::UINodeId> EditorToolbarGroup::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, UI::UILayoutStyle layout)
{
    layout.flexContainer.direction = UI::UIFlexDirection::Row;
    layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    if (layout.flexContainer.gap == UI::UILayoutGap{}) {
        layout.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space1);
    }
    if (layout.padding == UI::UIEdgeSpacing{}) {
        layout.padding = UI::UIEdgeSpacing::All(theme.spacing.space1);
    }
    UI::UIElementDescriptor descriptor = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Filled}, layout);
    descriptor.visual.boxPaint = UI::makePanelBoxPaint(
        theme, UI::scaleColorAlpha(theme.colors.surfaceContainerLow, 248));
    descriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    descriptor.semantics.mode = UI::UISemanticsMode::Automatic;
    return tree.createElement(parent, descriptor);
}

Core::Result<UI::UIIconButtonParts> EditorIconButton::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, EditorIcon icon,
    std::string_view accessibleName, UI::UILayoutStyle layout,
    bool enabled, UI::UIButtonVariant variant)
{
    layout = iconButtonLayout(theme, layout);
    const UI::UIComponentBuildBudget budget{
        .nodes = 3U,
        .textBytes = accessibleName.size() * 2U,
        .behaviors = {.activate = 1U},
    };
    UI::UIElementDescriptor rootDescriptor = UI::makePanelElement(layout);
    rootDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UI::UISemanticsMode::Automatic;
    auto transactionResult = tree.beginBuildTransaction(
        parent, rootDescriptor, budget);
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    UI::UIIconButtonParts parts{.root = transaction.rootNodeId()};

    UI::UIElementDescriptor buttonDescriptor = UI::makeButtonElement(
        {}, iconButtonLayout(theme, {}));
    buttonDescriptor.text.reset();
    buttonDescriptor.image = editorIconImage(icon);
    buttonDescriptor.visual.styleRole = UI::styleRoleForButtonVariant(variant);
    buttonDescriptor.semantics.name = accessibleName;
    buttonDescriptor.semantics.useContentAsName = false;
    buttonDescriptor.enabled = enabled;
    auto button = transaction.createElement(parts.root, buttonDescriptor);
    if (!button) {
        return Core::failure(button.error());
    }
    parts.button = *button;
    parts.icon = *button;

    UI::UILayoutStyle tooltipLayout{};
    tooltipLayout.minMax.maxWidth =
        UI::UILayoutLength::Px(theme.controls.tooltipMaxWidth);
    auto tooltip = transaction.createElement(
        parts.root,
        UI::makeTooltipElement(accessibleName, {}, tooltipLayout));
    if (!tooltip) {
        return Core::failure(tooltip.error());
    }
    parts.tooltip = *tooltip;
    if (Core::Status linked = tree.setTooltipAnchor(parts.tooltip, parts.button);
        !linked) {
        return Core::failure(linked.error());
    }
    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

Core::Result<EditorIconToggleButtonParts> EditorIconToggleButton::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, EditorIcon icon,
    std::string_view accessibleName, UI::UILayoutStyle layout, bool enabled)
{
    const UI::UIComponentBuildBudget budget{
        .nodes = 3U,
        .textBytes = accessibleName.size() * 2U,
        .behaviors = {.activate = 1U},
    };
    UI::UIElementDescriptor rootDescriptor =
        UI::makePanelElement(layout);
    rootDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UI::UISemanticsMode::Automatic;
    auto transactionResult = tree.beginBuildTransaction(
        parent, rootDescriptor, budget);
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    EditorIconToggleButtonParts parts{.root = transaction.rootNodeId()};

    UI::UIElementDescriptor buttonDescriptor = UI::makeRadioButtonElement(
        {}, iconButtonLayout(theme, {}));
    buttonDescriptor.text.reset();
    buttonDescriptor.contentAlignment.horizontal = UI::UIAxisAlignment::Center;
    buttonDescriptor.visual.styleRole = UI::UIStyleRoleId::SegmentedButton;
    buttonDescriptor.image = editorIconImage(icon);
    buttonDescriptor.semantics.name = accessibleName;
    buttonDescriptor.semantics.useContentAsName = false;
    buttonDescriptor.enabled = enabled;
    auto button = transaction.createElement(parts.root, buttonDescriptor);
    if (!button) {
        return Core::failure(button.error());
    }
    parts.button = *button;

    UI::UILayoutStyle tooltipLayout{};
    tooltipLayout.minMax.maxWidth =
        UI::UILayoutLength::Px(theme.controls.tooltipMaxWidth);
    auto tooltip = transaction.createElement(
        parts.root,
        UI::makeTooltipElement(accessibleName, {}, tooltipLayout));
    if (!tooltip) {
        return Core::failure(tooltip.error());
    }
    parts.tooltip = *tooltip;
    if (Core::Status linked = tree.setTooltipAnchor(parts.tooltip, parts.button);
        !linked) {
        return Core::failure(linked.error());
    }
    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

Core::Result<EditorPanelHeaderParts> EditorPanelHeader::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, std::string_view title,
    const UI::UITextStyle& textStyle, UI::UILayoutStyle layout)
{
    layout.flexContainer.direction = UI::UIFlexDirection::Row;
    layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    if (layout.flexContainer.gap == UI::UILayoutGap{}) {
        layout.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space2);
    }
    if (layout.padding == UI::UIEdgeSpacing{}) {
        layout.padding = UI::UIEdgeSpacing::HorizontalVertical(
            theme.spacing.space4, theme.spacing.space2);
    }
    UI::UIElementDescriptor rootDescriptor = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Filled}, layout);
    rootDescriptor.visual.boxPaint = UI::makeSolidBox(
        theme.colors.surfaceContainerLow);
    auto transactionResult = tree.beginBuildTransaction(
        parent, rootDescriptor,
        UI::UIComponentBuildBudget{
            .nodes = 3U,
            .textBytes = title.size(),
        });
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    EditorPanelHeaderParts parts{.root = transaction.rootNodeId()};

    UI::UILayoutStyle titleLayout{};
    titleLayout.flexItem.shrink = 1.0F;
    UI::UIElementDescriptor titleDescriptor =
        UI::makeLabelElement(title, titleLayout);
    titleDescriptor.textStyle = textStyle;
    auto titleNode = transaction.createElement(parts.root, titleDescriptor);
    if (!titleNode) {
        return Core::failure(titleNode.error());
    }
    parts.title = *titleNode;

    UI::UILayoutStyle actionsLayout{};
    actionsLayout.flexItem.grow = 1.0F;
    actionsLayout.flexItem.shrink = 1.0F;
    actionsLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    actionsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    actionsLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    actionsLayout.flexContainer.justifyContent = UI::UIJustifyContent::End;
    actionsLayout.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space1);
    auto actions = transaction.createElement(
        parts.root, UI::makePanelElement(actionsLayout));
    if (!actions) {
        return Core::failure(actions.error());
    }
    parts.actions = *actions;
    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

Core::Result<EditorSectionHeaderParts> EditorSectionHeader::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, std::string_view title,
    const UI::UITextStyle& textStyle, UI::UILayoutStyle layout)
{
    if (layout.size.width.isAuto()) {
        layout.size.width = UI::UILayoutLength::Percent(100.0F);
    }
    if (layout.size.height.isAuto()) {
        layout.size.height = UI::UILayoutLength::Px(
            theme.typography.section + theme.spacing.space2);
    }
    layout.flexItem.shrink = 0.0F;
    layout.flexContainer.direction = UI::UIFlexDirection::Row;
    layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    if (layout.flexContainer.gap == UI::UILayoutGap{}) {
        layout.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space3);
    }

    UI::UIElementDescriptor rootDescriptor = UI::makePanelElement(layout);
    rootDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UI::UISemanticsMode::Automatic;
    auto transactionResult = tree.beginBuildTransaction(
        parent, rootDescriptor,
        UI::UIComponentBuildBudget{
            .nodes = 3U,
            .textBytes = title.size(),
        });
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    EditorSectionHeaderParts parts{.root = transaction.rootNodeId()};

    UI::UILayoutStyle titleLayout{};
    titleLayout.flexItem.shrink = 0.0F;
    UI::UIElementDescriptor titleDescriptor =
        UI::makeLabelElement(title, titleLayout);
    titleDescriptor.textStyle = textStyle;
    auto titleNode = transaction.createElement(parts.root, titleDescriptor);
    if (!titleNode) {
        return Core::failure(titleNode.error());
    }
    parts.title = *titleNode;

    UI::UILayoutStyle dividerLayout{};
    dividerLayout.size.width = UI::UILayoutLength::Auto();
    dividerLayout.flexItem.grow = 1.0F;
    dividerLayout.flexItem.shrink = 1.0F;
    dividerLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    dividerLayout.flexItem.alignSelf = UI::UIAlignSelf::Center;
    auto divider = transaction.createElement(
        parts.root,
        UI::makeDividerElement(
            {.orientation = UI::UIDividerOrientation::Horizontal,
             .tone = UI::UIDividerTone::Subtle},
            dividerLayout));
    if (!divider) {
        return Core::failure(divider.error());
    }
    parts.divider = *divider;

    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

Core::Result<EditorPropertyRowParts> EditorPropertyRow::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, std::string_view label,
    const UI::UITextStyle& labelStyle, UI::UILayoutStyle layout,
    float labelWidth, UI::UIGridTrackList valueColumns)
{
    layout.containerLayout = UI::UIContainerLayout::Grid;
    layout.gridContainer.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Px(labelWidth),
        UI::UIGridTrack::Fr(),
    });
    layout.gridContainer.rows =
        UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
    layout.gridContainer.alignItems = UI::UIAxisAlignment::Center;
    layout.gridContainer.gap.row = 0.0F;
    layout.gridContainer.gap.column = theme.spacing.space3;
    layout.flexItem.shrink = 0.0F;
    auto transactionResult = tree.beginBuildTransaction(
        parent, UI::makePanelElement(layout),
        UI::UIComponentBuildBudget{
            .nodes = 3U,
            .textBytes = label.size(),
        });
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    EditorPropertyRowParts parts{
        .root = transaction.rootNodeId(),
        .rootLayout = layout,
    };

    UI::UILayoutStyle labelLayout{};
    labelLayout.gridItem.row = 0U;
    labelLayout.gridItem.column = 0U;
    labelLayout.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
    UI::UIElementDescriptor labelDescriptor =
        UI::makeLabelElement(label, labelLayout);
    labelDescriptor.textStyle = labelStyle;
    labelDescriptor.contentAlignment.vertical = UI::UIAxisAlignment::Center;
    auto labelNode = transaction.createElement(parts.root, labelDescriptor);
    if (!labelNode) {
        return Core::failure(labelNode.error());
    }
    parts.label = *labelNode;

    UI::UILayoutStyle valueLayout{};
    valueLayout.gridItem.row = 0U;
    valueLayout.gridItem.column = 1U;
    valueLayout.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
    valueLayout.containerLayout = UI::UIContainerLayout::Grid;
    valueLayout.gridContainer.columns = valueColumns;
    valueLayout.gridContainer.rows =
        UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
    valueLayout.gridContainer.alignItems = UI::UIAxisAlignment::Center;
    valueLayout.gridContainer.gap.column = theme.spacing.space2;
    auto value = transaction.createElement(
        parts.root, UI::makePanelElement(valueLayout));
    if (!value) {
        return Core::failure(value.error());
    }
    parts.value = *value;
    parts.valueLayout = valueLayout;
    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

Core::Result<EditorSearchFieldParts> EditorSearchField::Build(
    PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    const UI::UITheme& theme, std::string_view value,
    std::string_view accessibleName, UI::UILayoutStyle layout, bool enabled)
{
    layout.flexContainer.direction = UI::UIFlexDirection::Row;
    layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    layout.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space2);
    layout.padding = UI::UIEdgeSpacing::HorizontalVertical(
        theme.spacing.space3, theme.spacing.space1);
    UI::UIElementDescriptor rootDescriptor = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Filled}, layout);
    rootDescriptor.visual.boxPaint = UI::makeSolidBox(
        theme.colors.surfaceContainer, theme.controls.controlCornerRadius);
    auto transactionResult = tree.beginBuildTransaction(
        parent, rootDescriptor,
        UI::UIComponentBuildBudget{
            .nodes = 3U,
            .textBytes = value.size() + accessibleName.size(),
            .behaviors = {.textInput = 1U},
        });
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    PrimaryWindowUIBuildTransaction transaction =
        std::move(*transactionResult);
    EditorSearchFieldParts parts{.root = transaction.rootNodeId()};

    UI::UIElementDescriptor iconDescriptor = UI::makeIconElement(
        editorIconContent(EditorIcon::Search), iconLayout(theme));
    iconDescriptor.visual.styleRole = UI::UIStyleRoleId::IconOnSurface;
    auto icon = transaction.createElement(parts.root, iconDescriptor);
    if (!icon) {
        return Core::failure(icon.error());
    }
    parts.icon = *icon;

    UI::UILayoutStyle inputLayout{};
    inputLayout.size.height =
        UI::UILayoutLength::Px(theme.controls.textEditHeight);
    inputLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(
        theme.spacing.space4, theme.spacing.space1);
    inputLayout.flexItem.grow = 1.0F;
    inputLayout.flexItem.shrink = 1.0F;
    inputLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UIElementDescriptor inputDescriptor =
        UI::makeTextEditElement(value, inputLayout);
    inputDescriptor.visual.boxPaint = UI::makeSolidBox(
        UI::scaleColorAlpha(theme.colors.surface, 1));
    inputDescriptor.semantics.name = accessibleName;
    inputDescriptor.enabled = enabled;
    auto input = transaction.createElement(parts.root, inputDescriptor);
    if (!input) {
        return Core::failure(input.error());
    }
    parts.textEdit = *input;
    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

} // namespace Tina::EditorApp::WorkspaceInternal
