#include <tina/ui/UIContext.hpp>

#include "UIElementContractResolver.hpp"

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::UI {

namespace {

[[nodiscard]] Core::Status validateRequiredText(
    std::string_view text, std::string_view emptyMessage,
    std::string_view invalidUtf8Message) noexcept
{
    if (text.empty())
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor, emptyMessage);
    }
    if (!Core::isStrictUtf8WithoutNul(text))
    {
        return Core::failure(UIErrorCode::InvalidText, invalidUtf8Message);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateOptionalText(
    const std::optional<std::string_view>& text,
    std::string_view invalidUtf8Message) noexcept
{
    if (text.has_value() && !Core::isStrictUtf8WithoutNul(*text))
    {
        return Core::failure(UIErrorCode::InvalidText, invalidUtf8Message);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateText(std::string_view text,
                                        std::string_view invalidUtf8Message) noexcept
{
    if (!Core::isStrictUtf8WithoutNul(text))
    {
        return Core::failure(UIErrorCode::InvalidText, invalidUtf8Message);
    }
    return Core::success();
}

[[nodiscard]] Core::Status addTextBytes(UIComponentBuildBudget& budget,
                                        std::string_view text) noexcept
{
    if (text.size() > (std::numeric_limits<usize>::max)() - budget.textBytes)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI component text budget overflows usize");
    }
    budget.textBytes += text.size();
    return Core::success();
}

// Multiplication sign rather than the letter x. The Button publishes the
// caller's accessible name instead of this glyph.
[[nodiscard]] constexpr std::string_view dialogCloseGlyph() noexcept
{
    return "\xC3\x97";
}

[[nodiscard]] Core::Status validateDialogLength(
    UILayoutLength length, std::string_view message) noexcept
{
    if (length.isAuto())
    {
        return Core::success();
    }
    if (!std::isfinite(length.value) || length.value < 0.0F)
    {
        return Core::failure(UIErrorCode::InvalidLayout, message);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateTooltipProfile(
    std::string_view text, const UITooltipConfig& config,
    std::string_view emptyMessage, std::string_view invalidUtf8Message) noexcept
{
    if (Core::Status valid =
            validateRequiredText(text, emptyMessage, invalidUtf8Message);
        !valid)
    {
        return valid;
    }
    auto kind = Detail::resolveElementBuiltinKind(makeTooltipElement(text, config));
    if (!kind)
    {
        return Core::failure(kind.error());
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateAction(
    const UIFormFieldActionConfig& action) noexcept
{
    if (Core::Status valid = validateRequiredText(
            action.accessibleName,
            "UIFormField action accessible name must not be empty",
            "UIFormField action accessible name must be strict UTF-8 without NUL");
        !valid)
    {
        return valid;
    }
    if (Core::Status valid = validateOptionalText(
            action.accessibleDescription,
            "UIFormField action accessible description must be strict UTF-8 without NUL");
        !valid)
    {
        return valid;
    }
    if (!isValidButtonVariant(action.variant))
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UIFormField action Button variant is invalid");
    }
    if (action.tooltipText.has_value())
    {
        return validateTooltipProfile(
            *action.tooltipText, action.tooltip,
            "UIFormField action Tooltip text must not be empty",
            "UIFormField action Tooltip text must be strict UTF-8 without NUL");
    }
    return Core::success();
}

[[nodiscard]] UIStyleRoleId iconRoleForVariant(UIButtonVariant variant) noexcept
{
    switch (variant)
    {
    case UIButtonVariant::Primary:
        return UIStyleRoleId::IconOnPrimary;
    case UIButtonVariant::Danger:
        return UIStyleRoleId::IconOnError;
    case UIButtonVariant::Tonal:
    case UIButtonVariant::Outlined:
    case UIButtonVariant::Text:
        return UIStyleRoleId::IconOnSurface;
    }
    return static_cast<UIStyleRoleId>(0xFFU);
}

[[nodiscard]] UILayoutStyle iconButtonLayout(const UITheme& theme) noexcept
{
    UILayoutStyle layout{};
    layout.size.width = UILayoutLength::Px(theme.controls.iconButtonExtent);
    layout.size.height = UILayoutLength::Px(theme.controls.iconButtonExtent);
    layout.flexContainer.justifyContent = UIJustifyContent::Center;
    layout.flexContainer.alignItems = UIAxisAlignment::Center;
    return layout;
}

[[nodiscard]] UILayoutStyle iconLayout(const UITheme& theme) noexcept
{
    UILayoutStyle layout{};
    layout.size.width = UILayoutLength::Px(theme.controls.iconExtent);
    layout.size.height = UILayoutLength::Px(theme.controls.iconExtent);
    layout.flexItem.alignSelf = UIAlignSelf::Center;
    return layout;
}

[[nodiscard]] UIElementDescriptor makeProfileButton(
    std::string_view name, const std::optional<std::string_view>& description,
    UIButtonVariant variant, bool enabled, const UITheme& theme) noexcept
{
    UIElementDescriptor descriptor = makeButtonElement({}, iconButtonLayout(theme));
    descriptor.visual.styleRole = styleRoleForButtonVariant(variant);
    descriptor.semantics.name = name;
    descriptor.semantics.description = description;
    descriptor.semantics.useContentAsName = false;
    descriptor.enabled = enabled;
    return descriptor;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UIIconButtonParts>
buildIconButtonImpl(Authoring& authoring, UINodeId parent,
                    const UIIconButtonConfig& config, const UITheme& theme)
{
    auto budget = requiredIconButtonBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }

    UIElementDescriptor rootDescriptor = makePanelElement(config.layout);
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    rootDescriptor.layout.flexContainer.alignItems = UIAxisAlignment::Start;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UIIconButtonParts parts{.root = transaction.rootNodeId()};

    auto button = transaction.createElement(
        parts.root,
        makeProfileButton(config.accessibleName, config.accessibleDescription,
                          config.variant, config.enabled, theme));
    if (!button)
    {
        return Core::failure(button.error());
    }
    parts.button = *button;

    UIElementDescriptor iconDescriptor = makeIconElement(config.icon, iconLayout(theme));
    if (config.useThemeIconTint)
    {
        iconDescriptor.visual.styleRole = iconRoleForVariant(config.variant);
    }
    auto icon = transaction.createElement(parts.button, iconDescriptor);
    if (!icon)
    {
        return Core::failure(icon.error());
    }
    parts.icon = *icon;

    if (config.tooltipText.has_value())
    {
        UILayoutStyle tooltipLayout{};
        tooltipLayout.minMax.maxWidth =
            UILayoutLength::Px(theme.controls.tooltipMaxWidth);
        auto tooltip = transaction.createElement(
            parts.root,
            makeTooltipElement(*config.tooltipText, config.tooltip, tooltipLayout));
        if (!tooltip)
        {
            return Core::failure(tooltip.error());
        }
        parts.tooltip = *tooltip;
        if (Core::Status linked = authoring.setTooltipAnchor(parts.tooltip, parts.button);
            !linked)
        {
            return Core::failure(linked.error());
        }
    }

    auto committed = transaction.commit();
    if (!committed)
    {
        return Core::failure(committed.error());
    }
    return parts;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UIFormFieldActionParts>
buildFormFieldAction(Authoring& authoring, UIElementBuildTransaction& transaction,
                     UINodeId row, UINodeId tooltipParent,
                     const UIFormFieldActionConfig& config, const UITheme& theme)
{
    UIFormFieldActionParts parts{};
    auto button = transaction.createElement(
        row, makeProfileButton(config.accessibleName, config.accessibleDescription,
                               config.variant, config.enabled, theme));
    if (!button)
    {
        return Core::failure(button.error());
    }
    parts.button = *button;

    UIElementDescriptor iconDescriptor = makeIconElement(config.icon, iconLayout(theme));
    if (config.useThemeIconTint)
    {
        iconDescriptor.visual.styleRole = iconRoleForVariant(config.variant);
    }
    auto icon = transaction.createElement(parts.button, iconDescriptor);
    if (!icon)
    {
        return Core::failure(icon.error());
    }
    parts.icon = *icon;

    if (config.tooltipText.has_value())
    {
        UILayoutStyle tooltipLayout{};
        tooltipLayout.minMax.maxWidth =
            UILayoutLength::Px(theme.controls.tooltipMaxWidth);
        auto tooltip = transaction.createElement(
            tooltipParent,
            makeTooltipElement(*config.tooltipText, config.tooltip, tooltipLayout));
        if (!tooltip)
        {
            return Core::failure(tooltip.error());
        }
        parts.tooltip = *tooltip;
        if (Core::Status linked = authoring.setTooltipAnchor(parts.tooltip, parts.button);
            !linked)
        {
            return Core::failure(linked.error());
        }
    }
    return parts;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UIFormFieldParts>
buildFormFieldImpl(Authoring& authoring, UINodeId parent,
                   const UIFormFieldConfig& config, const UITheme& theme)
{
    auto budget = requiredFormFieldBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }

    UILayoutStyle rootLayout = config.layout;
    rootLayout.flexContainer.direction = UIFlexDirection::Column;
    if (rootLayout.flexContainer.gap == UILayoutGap{})
    {
        rootLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    }
    UIElementDescriptor rootDescriptor = makePanelElement(rootLayout);
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UIFormFieldParts parts{.root = transaction.rootNodeId()};

    UIElementDescriptor labelDescriptor = makeLabelElement(config.label);
    labelDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
    auto label = transaction.createElement(parts.root, labelDescriptor);
    if (!label)
    {
        return Core::failure(label.error());
    }
    parts.label = *label;

    UILayoutStyle rowLayout{};
    rowLayout.flexContainer.direction = UIFlexDirection::Row;
    rowLayout.flexContainer.alignItems = UIAxisAlignment::Center;
    rowLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    auto row = transaction.createElement(parts.root, makePanelElement(rowLayout));
    if (!row)
    {
        return Core::failure(row.error());
    }
    parts.inputRow = *row;

    if (config.leadingAction.has_value())
    {
        auto action = buildFormFieldAction(
            authoring, transaction, parts.inputRow, parts.root,
            *config.leadingAction, theme);
        if (!action)
        {
            return Core::failure(action.error());
        }
        parts.leadingAction = *action;
    }

    UILayoutStyle textEditLayout = config.textEditLayout;
    if (textEditLayout.size.height.isAuto())
    {
        textEditLayout.size.height =
            UILayoutLength::Px(theme.controls.textEditHeight);
    }
    if (textEditLayout.flexItem.grow == 0.0F &&
        textEditLayout.flexItem.basis.isAuto())
    {
        textEditLayout.flexItem.grow = 1.0F;
    }
    UIElementDescriptor textEditDescriptor =
        makeTextEditElement(config.value, textEditLayout);
    textEditDescriptor.visual.styleRole = config.errorText.has_value()
                                              ? UIStyleRoleId::TextInputInvalid
                                              : UIStyleRoleId::TextInput;
    textEditDescriptor.semantics.name = config.label;
    textEditDescriptor.semantics.description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    textEditDescriptor.textEditMultiline = config.multiline;
    textEditDescriptor.enabled = config.enabled;
    auto textEdit = transaction.createElement(parts.inputRow, textEditDescriptor);
    if (!textEdit)
    {
        return Core::failure(textEdit.error());
    }
    parts.textEdit = *textEdit;

    if (config.trailingAction.has_value())
    {
        auto action = buildFormFieldAction(
            authoring, transaction, parts.inputRow, parts.root,
            *config.trailingAction, theme);
        if (!action)
        {
            return Core::failure(action.error());
        }
        parts.trailingAction = *action;
    }

    if (config.helperText.has_value())
    {
        UIElementDescriptor helperDescriptor = makeLabelElement(*config.helperText);
        helperDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
        auto helper = transaction.createElement(parts.root, helperDescriptor);
        if (!helper)
        {
            return Core::failure(helper.error());
        }
        parts.helperText = *helper;
    }
    if (config.errorText.has_value())
    {
        UIElementDescriptor errorDescriptor = makeLabelElement(*config.errorText);
        errorDescriptor.visual.styleRole = UIStyleRoleId::TextError;
        auto error = transaction.createElement(parts.root, errorDescriptor);
        if (!error)
        {
            return Core::failure(error.error());
        }
        parts.errorText = *error;
    }

    auto committed = transaction.commit();
    if (!committed)
    {
        return Core::failure(committed.error());
    }
    return parts;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UIDialogParts>
buildDialogImpl(Authoring& authoring, UINodeId parent,
                const UIDialogConfig& config, const UITheme& theme)
{
    auto budget = requiredDialogBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }

    UILayoutStyle modalLayout = config.layout;
    modalLayout.placement = UILayoutPlacement::Overlay;
    modalLayout.overlay.horizontal = UIAxisAlignment::Stretch;
    modalLayout.overlay.vertical = UIAxisAlignment::Stretch;
    UIElementDescriptor modalDescriptor = makeModalElement(modalLayout);
    modalDescriptor.visual.styleRole = UIStyleRoleId::ModalScrim;
    modalDescriptor.semantics.name = config.title;
    modalDescriptor.semantics.description = config.body;
    // Ignore keeps blockedByModal meaningful: an outside press stays a targetless
    // barrier hit, which is what preserves focus inside the dialog. Scrim-press
    // dismissal needs its own committed-geometry check rather than a hit target.
    modalDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, modalDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UIDialogParts parts{.modal = transaction.rootNodeId()};

    UILayoutStyle surfaceLayout = config.surfaceLayout;
    surfaceLayout.placement = UILayoutPlacement::Overlay;
    surfaceLayout.overlay.horizontal = UIAxisAlignment::Center;
    surfaceLayout.overlay.vertical = UIAxisAlignment::Center;
    // dialogMinWidth is a floor, not a fixed width: the Surface sizes to its
    // content and is then clamped on both axes so long content cannot grow the
    // dialog past the viewport.
    if (surfaceLayout.minMax.minWidth.isAuto())
    {
        surfaceLayout.minMax.minWidth =
            config.style.minWidth.isAuto()
                ? UILayoutLength::Px(theme.controls.dialogMinWidth)
                : config.style.minWidth;
    }
    if (surfaceLayout.minMax.maxWidth.isAuto())
    {
        surfaceLayout.minMax.maxWidth = config.style.maxWidth.isAuto()
                                            ? UILayoutLength::Percent(100.0F)
                                            : config.style.maxWidth;
    }
    if (surfaceLayout.minMax.maxHeight.isAuto())
    {
        surfaceLayout.minMax.maxHeight = config.style.maxHeight.isAuto()
                                             ? UILayoutLength::Percent(100.0F)
                                             : config.style.maxHeight;
    }
    if (surfaceLayout.margin == UIEdgeSpacing{})
    {
        surfaceLayout.margin = UIEdgeSpacing::All(config.style.viewportMargin);
    }
    if (surfaceLayout.padding == UIEdgeSpacing{})
    {
        surfaceLayout.padding = UIEdgeSpacing::All(theme.spacing.space7);
    }
    surfaceLayout.flexContainer.direction = UIFlexDirection::Column;
    if (surfaceLayout.flexContainer.gap == UILayoutGap{})
    {
        surfaceLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space5);
    }
    UIElementDescriptor surfaceDescriptor = makePanelElement(surfaceLayout);
    surfaceDescriptor.visual.styleRole = UIStyleRoleId::ModalSurface;
    surfaceDescriptor.pointerHitPolicy = UIPointerHitPolicy::Targetable;
    auto surface = transaction.createElement(parts.modal, surfaceDescriptor);
    if (!surface)
    {
        return Core::failure(surface.error());
    }
    parts.surface = *surface;

    // Header owns the title and the optional close Button so the title can grow
    // while the Button stays pinned trailing. It never shrinks under a height
    // clamp; only the content region does.
    UILayoutStyle headerLayout{};
    headerLayout.flexContainer.direction = UIFlexDirection::Row;
    headerLayout.flexContainer.alignItems = UIAxisAlignment::Center;
    headerLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space4);
    headerLayout.flexItem.shrink = 0.0F;
    auto header = transaction.createElement(
        parts.surface, makePanelElement(headerLayout));
    if (!header)
    {
        return Core::failure(header.error());
    }
    parts.header = *header;

    UILayoutStyle titleLayout{};
    titleLayout.flexItem.grow = 1.0F;
    UIElementDescriptor titleDescriptor =
        makeLabelElement(config.title, titleLayout);
    titleDescriptor.visual.styleRole = UIStyleRoleId::TextTitle;
    auto title = transaction.createElement(parts.header, titleDescriptor);
    if (!title)
    {
        return Core::failure(title.error());
    }
    parts.title = *title;

    if (config.closeButtonName.has_value())
    {
        UILayoutStyle closeLayout{};
        closeLayout.size.width =
            UILayoutLength::Px(theme.controls.iconButtonExtent);
        closeLayout.size.height =
            UILayoutLength::Px(theme.controls.iconButtonExtent);
        closeLayout.flexItem.shrink = 0.0F;
        UIElementDescriptor closeDescriptor =
            makeButtonElement(dialogCloseGlyph(), closeLayout);
        closeDescriptor.visual.styleRole = UIStyleRoleId::ButtonText;
        // The glyph is not a usable accessible name, so publish the caller's
        // name instead of the content.
        closeDescriptor.semantics.useContentAsName = false;
        closeDescriptor.semantics.name = *config.closeButtonName;
        auto closeButton =
            transaction.createElement(parts.header, closeDescriptor);
        if (!closeButton)
        {
            return Core::failure(closeButton.error());
        }
        parts.closeButton = *closeButton;
    }

    // Content is created before the action row so caller-authored children
    // appended to it stay above the actions. The retained tree only appends, so
    // creation order is the only ordering control the caller has.
    UILayoutStyle contentLayout = config.contentLayout;
    if (contentLayout.flexContainer.gap == UILayoutGap{})
    {
        contentLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space4);
    }
    // Absorbs the surface height clamp. minHeight 0 lets flex shrink it below
    // its content height, which is what makes Scroll reachable.
    contentLayout.flexItem.shrink = 1.0F;
    if (contentLayout.minMax.minHeight.isAuto())
    {
        contentLayout.minMax.minHeight = UILayoutLength::Px(0.0F);
    }
    const bool scrollContent =
        config.style.contentOverflow == UIDialogContentOverflow::Scroll;
    UIElementDescriptor contentDescriptor =
        scrollContent ? makeScrollViewElement(contentLayout)
                      : makePanelElement(contentLayout);
    auto content = transaction.createElement(parts.surface, contentDescriptor);
    if (!content)
    {
        return Core::failure(content.error());
    }
    parts.content = *content;

    if (config.body.has_value())
    {
        UIElementDescriptor bodyDescriptor = makeLabelElement(*config.body);
        bodyDescriptor.visual.styleRole = UIStyleRoleId::TextBody;
        auto body = transaction.createElement(parts.content, bodyDescriptor);
        if (!body)
        {
            return Core::failure(body.error());
        }
        parts.body = *body;
    }

    if (config.actions.empty())
    {
        auto committedWithoutActions = transaction.commit();
        if (!committedWithoutActions)
        {
            return Core::failure(committedWithoutActions.error());
        }
        return parts;
    }

    if (config.style.showActionDivider)
    {
        UILayoutStyle dividerLayout{};
        dividerLayout.size.width = UILayoutLength::Percent(100.0F);
        dividerLayout.flexItem.shrink = 0.0F;
        auto divider = transaction.createElement(
            parts.surface,
            makeDividerElement(
                UIDividerConfig{.thickness = theme.controls.panelBorderWidth},
                dividerLayout));
        if (!divider)
        {
            return Core::failure(divider.error());
        }
        parts.actionDivider = *divider;
    }

    UILayoutStyle actionRowLayout{};
    actionRowLayout.flexContainer.direction = UIFlexDirection::Row;
    actionRowLayout.flexContainer.justifyContent = UIJustifyContent::End;
    actionRowLayout.flexContainer.alignItems = UIAxisAlignment::Center;
    actionRowLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space4);
    actionRowLayout.flexItem.shrink = 0.0F;
    auto actionRow = transaction.createElement(
        parts.surface, makePanelElement(actionRowLayout));
    if (!actionRow)
    {
        return Core::failure(actionRow.error());
    }
    parts.actionRow = *actionRow;

    for (usize index = 0; index < config.actions.size(); ++index)
    {
        const UIDialogActionConfig& action = config.actions[index];
        UILayoutStyle actionLayout{};
        actionLayout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
        actionLayout.padding = UIEdgeSpacing::HorizontalVertical(
            theme.spacing.space5, theme.spacing.space2);
        UIElementDescriptor actionDescriptor =
            makeButtonElement(action.text, actionLayout);
        actionDescriptor.visual.styleRole =
            styleRoleForButtonVariant(action.variant);
        actionDescriptor.enabled = action.enabled;
        auto button = transaction.createElement(parts.actionRow, actionDescriptor);
        if (!button)
        {
            return Core::failure(button.error());
        }
        parts.actions[index] = *button;
    }
    parts.actionCount = config.actions.size();

    auto committed = transaction.commit();
    if (!committed)
    {
        return Core::failure(committed.error());
    }
    return parts;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UISnackbarHostParts>
buildSnackbarHostImpl(Authoring& authoring, UINodeId parent,
                      const UISnackbarHostConfig& config,
                      const UITheme& theme)
{
    auto budget = requiredSnackbarHostBuildBudget(config);
    if (!budget) {
        return Core::failure(budget.error());
    }

    UILayoutStyle rootLayout = config.layout;
    rootLayout.placement = UILayoutPlacement::Overlay;
    rootLayout.overlay.horizontal = UIAxisAlignment::Stretch;
    rootLayout.overlay.vertical = UIAxisAlignment::Stretch;
    rootLayout.visibility = UIVisibility::Collapsed;
    UIElementDescriptor rootDescriptor = makePanelElement(rootLayout);
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult) {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UISnackbarHostParts parts{.root = transaction.rootNodeId()};

    UILayoutStyle surfaceLayout = config.surfaceLayout;
    surfaceLayout.placement = UILayoutPlacement::Overlay;
    surfaceLayout.overlay.horizontal = UIAxisAlignment::Center;
    surfaceLayout.overlay.vertical = UIAxisAlignment::End;
    if (surfaceLayout.size.width.isAuto()) {
        surfaceLayout.size.width = UILayoutLength::Px(480.0F);
    }
    if (surfaceLayout.minMax.maxWidth.isAuto()) {
        surfaceLayout.minMax.maxWidth = UILayoutLength::Percent(100.0F);
    }
    if (surfaceLayout.margin == UIEdgeSpacing{}) {
        surfaceLayout.margin = UIEdgeSpacing::All(config.viewportMargin);
    }
    if (surfaceLayout.padding == UIEdgeSpacing{}) {
        surfaceLayout.padding = UIEdgeSpacing::HorizontalVertical(
            theme.spacing.space5, theme.spacing.space3);
    }
    surfaceLayout.flexContainer.direction = UIFlexDirection::Row;
    surfaceLayout.flexContainer.alignItems = UIAxisAlignment::Center;
    if (surfaceLayout.flexContainer.gap == UILayoutGap{}) {
        surfaceLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space4);
    }
    UIElementDescriptor surfaceDescriptor = makePanelElement(surfaceLayout);
    surfaceDescriptor.visual.styleRole = UIStyleRoleId::FloatingSurface;
    surfaceDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    auto surface = transaction.createElement(parts.root, surfaceDescriptor);
    if (!surface) {
        return Core::failure(surface.error());
    }
    parts.surface = *surface;

    UILayoutStyle toneBarLayout{};
    toneBarLayout.size.width = UILayoutLength::Px(4.0F);
    toneBarLayout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
    toneBarLayout.flexItem.shrink = 0.0F;
    UIElementDescriptor toneBarDescriptor = makePanelElement(toneBarLayout);
    toneBarDescriptor.visual.boxPaint = makeSolidBox(theme.colors.primary);
    toneBarDescriptor.semantics.mode = UISemanticsMode::Exclude;
    auto toneBar = transaction.createElement(parts.surface, toneBarDescriptor);
    if (!toneBar) {
        return Core::failure(toneBar.error());
    }
    parts.toneBar = *toneBar;

    UILayoutStyle messageLayout{};
    messageLayout.size.width = UILayoutLength::Auto();
    messageLayout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
    messageLayout.flexItem.grow = 1.0F;
    messageLayout.flexItem.shrink = 1.0F;
    messageLayout.flexItem.basis = UILayoutLength::Px(0.0F);
    UIElementDescriptor messageDescriptor = makeLabelElement({}, messageLayout);
    messageDescriptor.visual.styleRole = UIStyleRoleId::TextBody;
    messageDescriptor.semantics.mode = UISemanticsMode::Publish;
    messageDescriptor.semantics.role = UISemanticsRole::Label;
    messageDescriptor.semantics.useContentAsName = true;
    messageDescriptor.semantics.liveSetting = UISemanticsLiveSetting::Polite;
    auto message = transaction.createElement(parts.surface, messageDescriptor);
    if (!message) {
        return Core::failure(message.error());
    }
    parts.message = *message;

    UILayoutStyle actionLayout{};
    actionLayout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
    actionLayout.padding = UIEdgeSpacing::HorizontalVertical(
        theme.spacing.space4, theme.spacing.space2);
    actionLayout.visibility = UIVisibility::Collapsed;
    UIElementDescriptor actionDescriptor = makeButtonElement("Action", actionLayout);
    actionDescriptor.visual.styleRole = UIStyleRoleId::ButtonText;
    auto action = transaction.createElement(parts.surface, actionDescriptor);
    if (!action) {
        return Core::failure(action.error());
    }
    parts.action = *action;

    auto committed = transaction.commit();
    if (!committed) {
        return Core::failure(committed.error());
    }
    return parts;
}

} // namespace

Core::Result<UIComponentBuildBudget>
requiredIconButtonBuildBudget(const UIIconButtonConfig& config) noexcept
{
    if (Core::Status valid = validateRequiredText(
            config.accessibleName,
            "UIIconButton accessible name must not be empty",
            "UIIconButton accessible name must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.accessibleDescription,
            "UIIconButton accessible description must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (!isValidButtonVariant(config.variant))
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UIIconButton Button variant is invalid");
    }
    if (config.tooltipText.has_value())
    {
        if (Core::Status valid = validateTooltipProfile(
                *config.tooltipText, config.tooltip,
                "UIIconButton Tooltip text must not be empty",
                "UIIconButton Tooltip text must be strict UTF-8 without NUL");
            !valid)
        {
            return Core::failure(valid.error());
        }
    }

    UIComponentBuildBudget budget{
        .nodes = 3U + (config.tooltipText.has_value() ? 1U : 0U),
        .behaviors = {.activate = 1U},
    };
    if (Core::Status status = addTextBytes(budget, config.accessibleName); !status)
    {
        return Core::failure(status.error());
    }
    if (config.accessibleDescription.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *config.accessibleDescription);
            !status)
        {
            return Core::failure(status.error());
        }
    }
    if (config.tooltipText.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *config.tooltipText); !status)
        {
            return Core::failure(status.error());
        }
    }
    return budget;
}

Core::Result<UIComponentBuildBudget>
requiredFormFieldBuildBudget(const UIFormFieldConfig& config) noexcept
{
    if (Core::Status valid = validateRequiredText(
            config.label, "UIFormField label must not be empty",
            "UIFormField label must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateText(
            config.value, "UIFormField value must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(config.helperText,
                                                  "UIFormField helper text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(config.errorText,
                                                  "UIFormField error text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (config.leadingAction.has_value())
    {
        if (Core::Status valid = validateAction(*config.leadingAction);
            !valid)
        {
            return Core::failure(valid.error());
        }
    }
    if (config.trailingAction.has_value())
    {
        if (Core::Status valid = validateAction(*config.trailingAction);
            !valid)
        {
            return Core::failure(valid.error());
        }
    }

    const auto actionNodeCount = [](const std::optional<UIFormFieldActionConfig>& action) noexcept {
        return action.has_value() ? 2U + (action->tooltipText.has_value() ? 1U : 0U) : 0U;
    };
    UIComponentBuildBudget budget{
        .nodes = 4U + (config.helperText.has_value() ? 1U : 0U) +
                 (config.errorText.has_value() ? 1U : 0U) +
                 actionNodeCount(config.leadingAction) +
                 actionNodeCount(config.trailingAction),
        .behaviors = {
            .activate = (config.leadingAction.has_value() ? 1U : 0U) +
                        (config.trailingAction.has_value() ? 1U : 0U),
            .textInput = 1U,
        },
    };

    const auto addActionText = [&](const std::optional<UIFormFieldActionConfig>& action)
        -> Core::Status {
        if (!action.has_value())
        {
            return Core::success();
        }
        if (Core::Status status = addTextBytes(budget, action->accessibleName); !status)
        {
            return status;
        }
        if (action->accessibleDescription.has_value())
        {
            if (Core::Status status =
                    addTextBytes(budget, *action->accessibleDescription);
                !status)
            {
                return status;
            }
        }
        return action->tooltipText.has_value()
                   ? addTextBytes(budget, *action->tooltipText)
                   : Core::success();
    };

    for (std::string_view text : {config.label, config.value, config.label})
    {
        if (Core::Status status = addTextBytes(budget, text); !status)
        {
            return Core::failure(status.error());
        }
    }
    const std::optional<std::string_view>& description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    if (description.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *description); !status)
        {
            return Core::failure(status.error());
        }
    }
    if (config.helperText.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *config.helperText); !status)
        {
            return Core::failure(status.error());
        }
    }
    if (config.errorText.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *config.errorText); !status)
        {
            return Core::failure(status.error());
        }
    }
    if (Core::Status status = addActionText(config.leadingAction); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = addActionText(config.trailingAction); !status)
    {
        return Core::failure(status.error());
    }
    return budget;
}

Core::Result<UIComponentBuildBudget>
requiredDialogBuildBudget(const UIDialogConfig& config) noexcept
{
    if (Core::Status valid = validateRequiredText(
            config.title, "UIDialog title must not be empty",
            "UIDialog title must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.body, "UIDialog body must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (config.closeButtonName.has_value())
    {
        if (Core::Status valid = validateRequiredText(
                *config.closeButtonName,
                "UIDialog close Button name must not be empty",
                "UIDialog close Button name must be strict UTF-8 without NUL");
            !valid)
        {
            return Core::failure(valid.error());
        }
    }
    if (config.actions.size() > UIDialogMaximumActionCount)
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UIDialog exceeds its fixed action count");
    }
    if (!isValidDialogContentOverflow(config.style.contentOverflow))
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UIDialog content overflow mode is invalid");
    }
    if (!std::isfinite(config.style.viewportMargin) ||
        config.style.viewportMargin < 0.0F)
    {
        return Core::failure(UIErrorCode::InvalidLayout,
                             "UIDialog viewport margin must be finite and non-negative");
    }
    if (Core::Status valid = validateDialogLength(
            config.style.minWidth,
            "UIDialog minimum width must be finite and non-negative");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateDialogLength(
            config.style.maxWidth,
            "UIDialog maximum width must be finite and non-negative");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateDialogLength(
            config.style.maxHeight,
            "UIDialog maximum height must be finite and non-negative");
        !valid)
    {
        return Core::failure(valid.error());
    }
    for (const UIDialogActionConfig& action : config.actions)
    {
        if (Core::Status valid = validateRequiredText(
                action.text, "UIDialog action text must not be empty",
                "UIDialog action text must be strict UTF-8 without NUL");
            !valid)
        {
            return Core::failure(valid.error());
        }
        if (!isValidButtonVariant(action.variant))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UIDialog action Button variant is invalid");
        }
    }

    // modal, surface, header, title, content are unconditional. The action row
    // and its Divider only exist when the dialog has actions.
    const bool hasCloseButton = config.closeButtonName.has_value();
    const usize actionRowNodes =
        config.actions.empty()
            ? 0U
            : 1U + (config.style.showActionDivider ? 1U : 0U) +
                  config.actions.size();
    UIComponentBuildBudget budget{
        .nodes = 5U + (hasCloseButton ? 1U : 0U) +
                 (config.body.has_value() ? 1U : 0U) + actionRowNodes,
        .behaviors = {
            .activate = config.actions.size() + (hasCloseButton ? 1U : 0U),
            .scroll = config.style.contentOverflow ==
                              UIDialogContentOverflow::Scroll
                          ? 1U
                          : 0U,
        },
    };
    // The title pays twice: once as the Modal accessible name and once as the
    // header Label. The body likewise pays as the Modal description and as the
    // content Label.
    if (Core::Status status = addTextBytes(budget, config.title); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = addTextBytes(budget, config.title); !status)
    {
        return Core::failure(status.error());
    }
    if (config.body.has_value())
    {
        if (Core::Status status = addTextBytes(budget, *config.body); !status)
        {
            return Core::failure(status.error());
        }
        if (Core::Status status = addTextBytes(budget, *config.body); !status)
        {
            return Core::failure(status.error());
        }
    }
    if (hasCloseButton)
    {
        // Glyph content plus the published accessible name.
        if (Core::Status status = addTextBytes(budget, dialogCloseGlyph());
            !status)
        {
            return Core::failure(status.error());
        }
        if (Core::Status status =
                addTextBytes(budget, *config.closeButtonName);
            !status)
        {
            return Core::failure(status.error());
        }
    }
    for (const UIDialogActionConfig& action : config.actions)
    {
        if (Core::Status status = addTextBytes(budget, action.text); !status)
        {
            return Core::failure(status.error());
        }
    }
    return budget;
}

Core::Result<UIIconButtonParts>
UIContext::buildIconButton(UINodeId parent, const UIIconButtonConfig& config)
{
    return buildIconButtonImpl(*this, parent, config, productTheme());
}

Core::Result<UIFormFieldParts>
UIContext::buildFormField(UINodeId parent, const UIFormFieldConfig& config)
{
    return buildFormFieldImpl(*this, parent, config, productTheme());
}

Core::Result<UIDialogParts>
UIContext::buildDialog(UINodeId parent, const UIDialogConfig& config)
{
    return buildDialogImpl(*this, parent, config, productTheme());
}

Core::Result<UISnackbarHostParts>
UIContext::buildSnackbarHost(UINodeId parent,
                             const UISnackbarHostConfig& config)
{
    return buildSnackbarHostImpl(*this, parent, config, productTheme());
}

Core::Result<UIIconButtonParts>
UITreeUpdater::buildIconButton(UINodeId parent, const UIIconButtonConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildIconButtonImpl(*this, parent, config, m_context->productTheme());
}

Core::Result<UIFormFieldParts>
UITreeUpdater::buildFormField(UINodeId parent, const UIFormFieldConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildFormFieldImpl(*this, parent, config, m_context->productTheme());
}

Core::Result<UIDialogParts>
UITreeUpdater::buildDialog(UINodeId parent, const UIDialogConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildDialogImpl(*this, parent, config, m_context->productTheme());
}

Core::Result<UISnackbarHostParts>
UITreeUpdater::buildSnackbarHost(UINodeId parent,
                                 const UISnackbarHostConfig& config)
{
    if (m_context == nullptr) {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildSnackbarHostImpl(*this, parent, config,
                                 m_context->productTheme());
}

} // namespace Tina::UI
