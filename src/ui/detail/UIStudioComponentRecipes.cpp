#include <tina/ui/UIContext.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UICollapsibleSection.hpp>
#include <tina/ui/UIColorField.hpp>
#include <tina/ui/UIColorPicker.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UINumberField.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace Tina::UI {

namespace {

inline constexpr float ColorPickerChannelLabelWidth = 18.0F;
inline constexpr float ColorPickerChannelValueWidth = 30.0F;

[[nodiscard]] constexpr UIStyleRoleId colorPickerSliderRole(
    usize channelIndex) noexcept
{
    constexpr std::array roles{
        UIStyleRoleId::SliderRed,
        UIStyleRoleId::SliderGreen,
        UIStyleRoleId::SliderBlue,
        UIStyleRoleId::SliderAlpha,
    };
    return roles[channelIndex];
}

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

[[nodiscard]] Core::Status validateNumberFieldSpec(
    const UINumberFieldValueSpec& spec) noexcept
{
    if (!std::isfinite(spec.minValue) || !std::isfinite(spec.maxValue) ||
        !std::isfinite(spec.step) || !(spec.maxValue > spec.minValue) ||
        !(spec.step > 0.0F))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UINumberField range and step must be finite with min < max and step > 0");
    }
    if (spec.decimalPlaces > UINumberFieldMaximumDecimalPlaces)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UINumberField decimal places exceed the fixed supported maximum");
    }
    return Core::success();
}

[[nodiscard]] constexpr std::optional<u8> hexNibble(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<u8>(value - '0');
    }
    if (value >= 'A' && value <= 'F')
    {
        return static_cast<u8>(value - 'A' + 10);
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<u8>(value - 'a' + 10);
    }
    return std::nullopt;
}

[[nodiscard]] UILayoutStyle componentRootLayout(
    UILayoutStyle layout, const UITheme& theme) noexcept
{
    layout.flexContainer.direction = UIFlexDirection::Column;
    if (layout.flexContainer.gap == UILayoutGap{})
    {
        layout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    }
    return layout;
}

[[nodiscard]] UILayoutStyle componentRowLayout(const UITheme& theme) noexcept
{
    UILayoutStyle layout{};
    layout.flexContainer.direction = UIFlexDirection::Row;
    layout.flexContainer.alignItems = UIAxisAlignment::Center;
    layout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    return layout;
}

[[nodiscard]] UILayoutStyle numberFieldRootLayout(
    const UINumberFieldConfig& config, const UITheme& theme) noexcept
{
    UILayoutStyle layout = config.layout;
    layout.flexContainer.direction =
        config.labelPlacement == UINumberFieldLabelPlacement::Leading
            ? UIFlexDirection::Row
            : UIFlexDirection::Column;
    if (config.labelPlacement == UINumberFieldLabelPlacement::Leading)
    {
        layout.flexContainer.alignItems = UIAxisAlignment::Center;
    }
    if (layout.flexContainer.gap == UILayoutGap{})
    {
        layout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    }
    return layout;
}

[[nodiscard]] UILayoutStyle numberFieldContentLayout(
    const UITheme& theme) noexcept
{
    UILayoutStyle layout{};
    layout.flexItem.grow = 1.0F;
    layout.flexItem.shrink = 1.0F;
    layout.flexItem.basis = UILayoutLength::Px(0.0F);
    layout.flexContainer.direction = UIFlexDirection::Column;
    layout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    return layout;
}

[[nodiscard]] UILayoutStyle compactButtonLayout(const UITheme& theme) noexcept
{
    UILayoutStyle layout{};
    layout.size.width = UILayoutLength::Px(theme.controls.buttonHeight);
    layout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
    return layout;
}

[[nodiscard]] UIElementDescriptor makeCompactTextButton(
    std::string_view text, std::string_view accessibleName, bool enabled,
    const UITheme& theme) noexcept
{
    UIElementDescriptor descriptor =
        makeButtonElement(text, compactButtonLayout(theme));
    descriptor.visual.styleRole = UIStyleRoleId::ButtonOutlined;
    descriptor.semantics.name = accessibleName;
    descriptor.semantics.useContentAsName = false;
    descriptor.enabled = enabled;
    return descriptor;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UINumberFieldParts>
buildNumberFieldImpl(Authoring& authoring, UINodeId parent,
                     const UINumberFieldConfig& config, const UITheme& theme)
{
    auto budget = requiredNumberFieldBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }
    auto valueState = synchronizeNumberFieldValue(config.value, config.valueSpec);
    if (!valueState)
    {
        return Core::failure(valueState.error());
    }

    UIElementDescriptor rootDescriptor =
        makePanelElement(numberFieldRootLayout(config, theme));
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UINumberFieldParts parts{.root = transaction.rootNodeId()};

    UIElementDescriptor labelDescriptor =
        makeLabelElement(config.label, config.labelLayout);
    labelDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
    auto label = transaction.createElement(parts.root, labelDescriptor);
    if (!label)
    {
        return Core::failure(label.error());
    }
    parts.label = *label;

    UINodeId contentParent = parts.root;
    parts.content = parts.root;
    if (config.labelPlacement == UINumberFieldLabelPlacement::Leading)
    {
        auto content = transaction.createElement(
            parts.root,
            makePanelElement(numberFieldContentLayout(theme)));
        if (!content)
        {
            return Core::failure(content.error());
        }
        parts.content = *content;
        contentParent = *content;
    }

    auto inputRow = transaction.createElement(
        contentParent, makePanelElement(componentRowLayout(theme)));
    if (!inputRow)
    {
        return Core::failure(inputRow.error());
    }
    parts.inputRow = *inputRow;

    auto decrement = transaction.createElement(
        parts.inputRow,
        makeCompactTextButton("-", config.decrementAccessibleName,
                              config.enabled, theme));
    if (!decrement)
    {
        return Core::failure(decrement.error());
    }
    parts.decrementButton = *decrement;

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
        makeTextEditElement(valueState->text.view(), textEditLayout);
    textEditDescriptor.visual.styleRole = config.errorText.has_value()
                                              ? UIStyleRoleId::TextInputInvalid
                                              : UIStyleRoleId::TextInput;
    textEditDescriptor.semantics.name = config.label;
    textEditDescriptor.semantics.description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    textEditDescriptor.enabled = config.enabled;
    auto textEdit = transaction.createElement(parts.inputRow, textEditDescriptor);
    if (!textEdit)
    {
        return Core::failure(textEdit.error());
    }
    parts.textEdit = *textEdit;

    auto increment = transaction.createElement(
        parts.inputRow,
        makeCompactTextButton("+", config.incrementAccessibleName,
                              config.enabled, theme));
    if (!increment)
    {
        return Core::failure(increment.error());
    }
    parts.incrementButton = *increment;

    if (config.helperText.has_value())
    {
        UIElementDescriptor helperDescriptor =
            makeLabelElement(*config.helperText);
        helperDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
        auto helper = transaction.createElement(contentParent, helperDescriptor);
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
        auto error = transaction.createElement(contentParent, errorDescriptor);
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

template <typename Authoring, typename InitializeToggle>
[[nodiscard]] Core::Result<UICollapsibleSectionParts>
buildCollapsibleSectionImpl(Authoring& authoring, UINodeId parent,
                            const UICollapsibleSectionConfig& config,
                            const UITheme& theme,
                            InitializeToggle&& initializeToggle)
{
    auto budget = requiredCollapsibleSectionBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }
    const UICollapsibleSectionState state =
        synchronizeCollapsibleSectionState(config.expanded);

    UIElementDescriptor rootDescriptor =
        makePanelElement(componentRootLayout(config.layout, theme));
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UICollapsibleSectionParts parts{.root = transaction.rootNodeId()};

    UILayoutStyle headerLayout = config.headerLayout;
    headerLayout.flexContainer.direction = UIFlexDirection::Row;
    headerLayout.flexContainer.alignItems = UIAxisAlignment::Center;
    if (headerLayout.flexContainer.gap == UILayoutGap{})
    {
        headerLayout.flexContainer.gap = UILayoutGap::All(theme.spacing.space2);
    }
    if (headerLayout.size.height.isAuto())
    {
        headerLayout.size.height = UILayoutLength::Px(theme.controls.buttonHeight);
    }
    UIElementDescriptor headerDescriptor = makeCheckboxElement(headerLayout);
    headerDescriptor.visual.styleRole = UIStyleRoleId::ButtonText;
    headerDescriptor.semantics.role = UISemanticsRole::Button;
    headerDescriptor.semantics.name = config.title;
    headerDescriptor.enabled = config.enabled;
    auto header = transaction.createElement(parts.root, headerDescriptor);
    if (!header)
    {
        return Core::failure(header.error());
    }
    parts.header = *header;

    if (Core::Status initialized =
            initializeToggle(transaction, parts.header, state.headerChecked);
        !initialized)
    {
        return Core::failure(initialized.error());
    }

    UILayoutStyle indicatorLayout = config.indicatorLayout;
    if (indicatorLayout.size.width.isAuto())
    {
        indicatorLayout.size.width = UILayoutLength::Px(theme.controls.iconExtent);
    }
    if (indicatorLayout.size.height.isAuto())
    {
        indicatorLayout.size.height = UILayoutLength::Px(theme.controls.iconExtent);
    }
    indicatorLayout.flexItem.alignSelf = UIAlignSelf::Center;

    UILayoutStyle collapsedIndicatorLayout = indicatorLayout;
    collapsedIndicatorLayout.visibility = state.collapsedIndicatorVisibility;
    UIElementDescriptor collapsedIndicatorDescriptor =
        makeIconElement(config.collapsedIndicator, collapsedIndicatorLayout);
    collapsedIndicatorDescriptor.visual.styleRole = UIStyleRoleId::IconOnSurface;
    auto collapsedIndicator = transaction.createElement(
        parts.header, collapsedIndicatorDescriptor);
    if (!collapsedIndicator)
    {
        return Core::failure(collapsedIndicator.error());
    }
    parts.collapsedIndicator = *collapsedIndicator;

    UILayoutStyle expandedIndicatorLayout = indicatorLayout;
    expandedIndicatorLayout.visibility = state.expandedIndicatorVisibility;
    UIElementDescriptor expandedIndicatorDescriptor =
        makeIconElement(config.expandedIndicator, expandedIndicatorLayout);
    expandedIndicatorDescriptor.visual.styleRole = UIStyleRoleId::IconOnSurface;
    auto expandedIndicator = transaction.createElement(
        parts.header, expandedIndicatorDescriptor);
    if (!expandedIndicator)
    {
        return Core::failure(expandedIndicator.error());
    }
    parts.expandedIndicator = *expandedIndicator;

    UIElementDescriptor titleDescriptor = makeLabelElement(config.title);
    titleDescriptor.visual.styleRole = UIStyleRoleId::TextBody;
    titleDescriptor.semantics.mode = UISemanticsMode::Exclude;
    titleDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    auto title = transaction.createElement(parts.header, titleDescriptor);
    if (!title)
    {
        return Core::failure(title.error());
    }
    parts.title = *title;

    UILayoutStyle contentLayout = config.contentLayout;
    contentLayout.visibility = state.contentVisibility;
    UIElementDescriptor contentDescriptor = makePanelElement(contentLayout);
    contentDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    contentDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto content = transaction.createElement(parts.root, contentDescriptor);
    if (!content)
    {
        return Core::failure(content.error());
    }
    parts.content = *content;

    auto committed = transaction.commit();
    if (!committed)
    {
        return Core::failure(committed.error());
    }
    return parts;
}

template <typename Authoring>
[[nodiscard]] Core::Result<UIColorFieldParts>
buildColorFieldImpl(Authoring& authoring, UINodeId parent,
                    const UIColorFieldConfig& config, const UITheme& theme)
{
    auto budget = requiredColorFieldBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }
    const UIColorFieldState state = synchronizeColorFieldValue(config.value);

    UIElementDescriptor rootDescriptor =
        makePanelElement(componentRootLayout(config.layout, theme));
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UIColorFieldParts parts{.root = transaction.rootNodeId()};

    UIElementDescriptor labelDescriptor = makeLabelElement(config.label);
    labelDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
    auto label = transaction.createElement(parts.root, labelDescriptor);
    if (!label)
    {
        return Core::failure(label.error());
    }
    parts.label = *label;

    auto inputRow = transaction.createElement(
        parts.root, makePanelElement(componentRowLayout(theme)));
    if (!inputRow)
    {
        return Core::failure(inputRow.error());
    }
    parts.inputRow = *inputRow;

    UIElementDescriptor swatchDescriptor =
        makeButtonElement({}, compactButtonLayout(theme));
    swatchDescriptor.visual.styleRole = UIStyleRoleId::ButtonOutlined;
    swatchDescriptor.visual.boxPaint =
        makePanelBoxPaint(theme, state.value, UIElevation::Flat);
    swatchDescriptor.semantics.name = config.swatchAccessibleName;
    swatchDescriptor.semantics.useContentAsName = false;
    swatchDescriptor.enabled = config.enabled;
    auto swatch = transaction.createElement(parts.inputRow, swatchDescriptor);
    if (!swatch)
    {
        return Core::failure(swatch.error());
    }
    parts.swatchButton = *swatch;

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
        makeTextEditElement(state.text.view(), textEditLayout);
    textEditDescriptor.visual.styleRole = config.errorText.has_value()
                                              ? UIStyleRoleId::TextInputInvalid
                                              : UIStyleRoleId::TextInput;
    textEditDescriptor.semantics.name = config.label;
    textEditDescriptor.semantics.description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    textEditDescriptor.enabled = config.enabled;
    auto textEdit = transaction.createElement(parts.inputRow, textEditDescriptor);
    if (!textEdit)
    {
        return Core::failure(textEdit.error());
    }
    parts.textEdit = *textEdit;

    if (config.helperText.has_value())
    {
        UIElementDescriptor helperDescriptor =
            makeLabelElement(*config.helperText);
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

template <typename Authoring, typename InitializeRange>
[[nodiscard]] Core::Result<UIColorPickerParts>
buildColorPickerImpl(Authoring& authoring, UINodeId parent,
                     const UIColorPickerConfig& config, const UITheme& theme,
                     InitializeRange&& initializeRange)
{
    auto budget = requiredColorPickerBuildBudget(config);
    if (!budget)
    {
        return Core::failure(budget.error());
    }
    const UIColorPickerState state =
        synchronizeColorPickerValue(config.value, config.includeAlpha);

    UIElementDescriptor rootDescriptor =
        makePanelElement(componentRootLayout(config.layout, theme));
    rootDescriptor.pointerHitPolicy = UIPointerHitPolicy::Ignore;
    rootDescriptor.semantics.mode = UISemanticsMode::Automatic;
    auto transactionResult = authoring.beginBuildTransaction(
        parent, rootDescriptor, *budget);
    if (!transactionResult)
    {
        return Core::failure(transactionResult.error());
    }
    UIElementBuildTransaction transaction = std::move(*transactionResult);
    UIColorPickerParts parts{.root = transaction.rootNodeId()};

    parts.channelCount = state.channelCount;
    for (usize index = 0; index < parts.channelCount; ++index)
    {
        auto row = transaction.createElement(
            parts.root, makePanelElement(componentRowLayout(theme)));
        if (!row)
        {
            return Core::failure(row.error());
        }
        parts.channelRows[index] = *row;

        UILayoutStyle labelLayout{};
        labelLayout.size.width =
            UILayoutLength::Px(ColorPickerChannelLabelWidth);
        labelLayout.flexItem.shrink = 0.0F;
        UIElementDescriptor labelDescriptor =
            makeLabelElement(config.channelLabels[index], labelLayout);
        labelDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
        labelDescriptor.contentAlignment = {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        };
        auto label = transaction.createElement(parts.channelRows[index],
                                               labelDescriptor);
        if (!label)
        {
            return Core::failure(label.error());
        }
        parts.channelLabels[index] = *label;

        UILayoutStyle sliderLayout{};
        sliderLayout.size.height =
            UILayoutLength::Px(theme.controls.sliderHeight);
        sliderLayout.flexItem.grow = 1.0F;
        UIElementDescriptor sliderDescriptor = makeSliderElement(sliderLayout);
        sliderDescriptor.visual.styleRole = colorPickerSliderRole(index);
        sliderDescriptor.semantics.name =
            config.channelAccessibleNames[index];
        sliderDescriptor.enabled = config.enabled;
        auto slider = transaction.createElement(parts.channelRows[index],
                                                sliderDescriptor);
        if (!slider)
        {
            return Core::failure(slider.error());
        }
        parts.channelSliders[index] = *slider;

        UILayoutStyle channelValueLayout{};
        channelValueLayout.size.width =
            UILayoutLength::Px(ColorPickerChannelValueWidth);
        channelValueLayout.flexItem.shrink = 0.0F;
        UIElementDescriptor channelValueDescriptor =
            makeLabelElement(state.channelTexts[index].view(), channelValueLayout);
        channelValueDescriptor.visual.styleRole = UIStyleRoleId::TextSecondary;
        channelValueDescriptor.contentAlignment = {
            .horizontal = UIAxisAlignment::End,
            .vertical = UIAxisAlignment::Center,
        };
        auto channelValue = transaction.createElement(
            parts.channelRows[index], channelValueDescriptor);
        if (!channelValue)
        {
            return Core::failure(channelValue.error());
        }
        parts.channelValueLabels[index] = *channelValue;

        if (Core::Status initialized = initializeRange(
                transaction, parts.channelSliders[index],
                state.channelValues[index]);
            !initialized)
        {
            return Core::failure(initialized.error());
        }
    }

    auto committed = transaction.commit();
    if (!committed)
    {
        return Core::failure(committed.error());
    }
    return parts;
}

} // namespace

Core::Result<float>
normalizeNumberFieldValue(float value,
                          const UINumberFieldValueSpec& spec) noexcept
{
    if (Core::Status valid = validateNumberFieldSpec(spec); !valid)
    {
        return Core::failure(valid.error());
    }
    if (!std::isfinite(value))
    {
        return Core::failure(UIErrorCode::InvalidControlValue,
                             "UINumberField value must be finite");
    }

    const double minValue = static_cast<double>(spec.minValue);
    const double maxValue = static_cast<double>(spec.maxValue);
    const double step = static_cast<double>(spec.step);
    const double clamped =
        (std::clamp)(static_cast<double>(value), minValue, maxValue);
    const double stepCount = std::round((clamped - minValue) / step);
    const double quantized =
        (std::clamp)(minValue + stepCount * step, minValue, maxValue);
    const float normalized = static_cast<float>(quantized);
    return normalized == 0.0F ? 0.0F : normalized;
}

Core::Result<float>
parseNumberFieldValue(std::string_view text,
                      const UINumberFieldValueSpec& spec) noexcept
{
    if (Core::Status valid = validateNumberFieldSpec(spec); !valid)
    {
        return Core::failure(valid.error());
    }
    if (text.empty())
    {
        return Core::failure(UIErrorCode::InvalidControlValue,
                             "UINumberField text must not be empty");
    }

    float parsed = 0.0F;
    const bool hasLeadingPlus = text.front() == '+';
    if (hasLeadingPlus && text.size() == 1U)
    {
        return Core::failure(UIErrorCode::InvalidControlValue,
                             "UINumberField sign must be followed by a number");
    }
    const char* const begin = text.data() + (hasLeadingPlus ? 1U : 0U);
    const char* const end = text.data() + text.size();
    const std::from_chars_result result =
        std::from_chars(begin, end, parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end ||
        !std::isfinite(parsed))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UINumberField text must be one complete finite number");
    }
    return normalizeNumberFieldValue(parsed, spec);
}

Core::Result<UINumberFieldText>
formatNumberFieldValue(float value,
                       const UINumberFieldValueSpec& spec) noexcept
{
    auto normalized = normalizeNumberFieldValue(value, spec);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }

    UINumberFieldText text{};
    char* const begin = text.bytes.data();
    char* const end = begin + text.bytes.size() - 1U;
    const std::to_chars_result result =
        std::to_chars(begin, end, *normalized, std::chars_format::fixed,
                      static_cast<int>(spec.decimalPlaces));
    if (result.ec != std::errc{})
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UINumberField formatted value exceeds its fixed buffer");
    }
    text.size = static_cast<usize>(result.ptr - begin);
    text.bytes[text.size] = '\0';
    return text;
}

Core::Result<UINumberFieldState>
synchronizeNumberFieldValue(float value,
                            const UINumberFieldValueSpec& spec) noexcept
{
    auto normalized = normalizeNumberFieldValue(value, spec);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    auto text = formatNumberFieldValue(*normalized, spec);
    if (!text)
    {
        return Core::failure(text.error());
    }
    return UINumberFieldState{.value = *normalized, .text = *text};
}

Core::Result<UINumberFieldState>
synchronizeNumberFieldText(std::string_view text,
                           const UINumberFieldValueSpec& spec) noexcept
{
    auto value = parseNumberFieldValue(text, spec);
    if (!value)
    {
        return Core::failure(value.error());
    }
    return synchronizeNumberFieldValue(*value, spec);
}

Core::Result<UINumberFieldState>
stepNumberFieldValue(float value, i32 stepCount,
                     const UINumberFieldValueSpec& spec) noexcept
{
    auto normalized = normalizeNumberFieldValue(value, spec);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    const double stepped = static_cast<double>(*normalized) +
                           static_cast<double>(stepCount) *
                               static_cast<double>(spec.step);
    const double clamped = (std::clamp)(
        stepped, static_cast<double>(spec.minValue),
        static_cast<double>(spec.maxValue));
    return synchronizeNumberFieldValue(static_cast<float>(clamped), spec);
}

Core::Result<UIComponentBuildBudget>
requiredNumberFieldBuildBudget(const UINumberFieldConfig& config) noexcept
{
    if (!isValidNumberFieldLabelPlacement(config.labelPlacement))
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UINumberField label placement is invalid");
    }
    if (Core::Status valid = validateRequiredText(
            config.label, "UINumberField label must not be empty",
            "UINumberField label must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateRequiredText(
            config.decrementAccessibleName,
            "UINumberField decrement accessible name must not be empty",
            "UINumberField decrement accessible name must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateRequiredText(
            config.incrementAccessibleName,
            "UINumberField increment accessible name must not be empty",
            "UINumberField increment accessible name must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.helperText,
            "UINumberField helper text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.errorText,
            "UINumberField error text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    auto valueState = synchronizeNumberFieldValue(config.value, config.valueSpec);
    if (!valueState)
    {
        return Core::failure(valueState.error());
    }

    UIComponentBuildBudget budget{
        .nodes = 6U +
                 (config.labelPlacement == UINumberFieldLabelPlacement::Leading
                      ? 1U
                      : 0U) +
                 (config.helperText.has_value() ? 1U : 0U) +
                 (config.errorText.has_value() ? 1U : 0U),
        .behaviors = {.activate = 2U, .textInput = 1U},
    };
    for (std::string_view text :
         {config.label, config.decrementAccessibleName, std::string_view{"-"},
          valueState->text.view(), config.label, config.incrementAccessibleName,
          std::string_view{"+"}})
    {
        if (Core::Status added = addTextBytes(budget, text); !added)
        {
            return Core::failure(added.error());
        }
    }
    const std::optional<std::string_view>& description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    if (description.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *description); !added)
        {
            return Core::failure(added.error());
        }
    }
    if (config.helperText.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *config.helperText); !added)
        {
            return Core::failure(added.error());
        }
    }
    if (config.errorText.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *config.errorText); !added)
        {
            return Core::failure(added.error());
        }
    }
    return budget;
}

Core::Result<UIComponentBuildBudget>
requiredCollapsibleSectionBuildBudget(
    const UICollapsibleSectionConfig& config) noexcept
{
    if (Core::Status valid = validateRequiredText(
            config.title, "UICollapsibleSection title must not be empty",
            "UICollapsibleSection title must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }

    UIComponentBuildBudget budget{
        .nodes = 6U,
        .behaviors = {.activate = 1U, .toggle = 1U},
    };
    for (std::string_view text : {config.title, config.title})
    {
        if (Core::Status added = addTextBytes(budget, text); !added)
        {
            return Core::failure(added.error());
        }
    }
    return budget;
}

Core::Result<UIStraightSrgba8Color>
parseColorFieldValue(std::string_view text) noexcept
{
    if (text.size() != UIColorFieldTextLength || text.front() != '#')
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UIColorField text must use the complete #RRGGBBAA form");
    }

    std::array<u8, 4> channels{};
    for (usize index = 0; index < channels.size(); ++index)
    {
        const std::optional<u8> high = hexNibble(text[1U + index * 2U]);
        const std::optional<u8> low = hexNibble(text[2U + index * 2U]);
        if (!high.has_value() || !low.has_value())
        {
            return Core::failure(
                UIErrorCode::InvalidControlValue,
                "UIColorField text contains a non-hexadecimal channel digit");
        }
        channels[index] = static_cast<u8>((*high << 4U) | *low);
    }
    return UIStraightSrgba8Color{
        .red = channels[0],
        .green = channels[1],
        .blue = channels[2],
        .alpha = channels[3],
    };
}

Core::Result<UIColorFieldState>
synchronizeColorFieldText(std::string_view text) noexcept
{
    auto value = parseColorFieldValue(text);
    if (!value)
    {
        return Core::failure(value.error());
    }
    return synchronizeColorFieldValue(*value);
}

Core::Result<UIComponentBuildBudget>
requiredColorFieldBuildBudget(const UIColorFieldConfig& config) noexcept
{
    if (Core::Status valid = validateRequiredText(
            config.label, "UIColorField label must not be empty",
            "UIColorField label must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateRequiredText(
            config.swatchAccessibleName,
            "UIColorField swatch accessible name must not be empty",
            "UIColorField swatch accessible name must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.helperText,
            "UIColorField helper text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }
    if (Core::Status valid = validateOptionalText(
            config.errorText,
            "UIColorField error text must be strict UTF-8 without NUL");
        !valid)
    {
        return Core::failure(valid.error());
    }

    const UIColorFieldState state = synchronizeColorFieldValue(config.value);
    UIComponentBuildBudget budget{
        .nodes = 5U + (config.helperText.has_value() ? 1U : 0U) +
                 (config.errorText.has_value() ? 1U : 0U),
        .behaviors = {.activate = 1U, .textInput = 1U},
    };
    for (std::string_view text :
         {config.label, config.swatchAccessibleName, state.text.view(),
          config.label})
    {
        if (Core::Status added = addTextBytes(budget, text); !added)
        {
            return Core::failure(added.error());
        }
    }
    const std::optional<std::string_view>& description =
        config.errorText.has_value() ? config.errorText : config.helperText;
    if (description.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *description); !added)
        {
            return Core::failure(added.error());
        }
    }
    if (config.helperText.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *config.helperText); !added)
        {
            return Core::failure(added.error());
        }
    }
    if (config.errorText.has_value())
    {
        if (Core::Status added = addTextBytes(budget, *config.errorText); !added)
        {
            return Core::failure(added.error());
        }
    }
    return budget;
}

Core::Result<UIColorPickerState>
synchronizeColorPickerChannel(UIStraightSrgba8Color currentValue,
                              bool includeAlpha, UIColorPickerChannel channel,
                              float channelValue) noexcept
{
    const u8 channelIndex = static_cast<u8>(channel);
    if (channelIndex >= UIColorPickerMaximumChannelCount ||
        (!includeAlpha && channel == UIColorPickerChannel::Alpha))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UIColorPicker channel is not present in this profile");
    }
    if (!std::isfinite(channelValue))
    {
        return Core::failure(UIErrorCode::InvalidControlValue,
                             "UIColorPicker channel value must be finite");
    }

    const float normalized = std::round((std::clamp)(channelValue, 0.0F, 255.0F));
    const u8 channelByte = static_cast<u8>(normalized);
    switch (channel)
    {
    case UIColorPickerChannel::Red:
        currentValue.red = channelByte;
        break;
    case UIColorPickerChannel::Green:
        currentValue.green = channelByte;
        break;
    case UIColorPickerChannel::Blue:
        currentValue.blue = channelByte;
        break;
    case UIColorPickerChannel::Alpha:
        currentValue.alpha = channelByte;
        break;
    }
    return synchronizeColorPickerValue(currentValue, includeAlpha);
}

Core::Result<UIComponentBuildBudget>
requiredColorPickerBuildBudget(const UIColorPickerConfig& config) noexcept
{
    const usize channelCount = config.includeAlpha ? 4U : 3U;
    for (usize index = 0; index < channelCount; ++index)
    {
        if (Core::Status valid = validateRequiredText(
                config.channelLabels[index],
                "UIColorPicker channel label must not be empty",
                "UIColorPicker channel label must be strict UTF-8 without NUL");
            !valid)
        {
            return Core::failure(valid.error());
        }
        if (Core::Status valid = validateRequiredText(
                config.channelAccessibleNames[index],
                "UIColorPicker channel accessible name must not be empty",
                "UIColorPicker channel accessible name must be strict UTF-8 without NUL");
            !valid)
        {
            return Core::failure(valid.error());
        }
    }

    UIComponentBuildBudget budget{
        .nodes = 1U + channelCount * 4U,
        .behaviors = {.range = channelCount},
    };
    const UIColorPickerState state =
        synchronizeColorPickerValue(config.value, config.includeAlpha);
    for (usize index = 0; index < channelCount; ++index)
    {
        if (Core::Status added = addTextBytes(
                budget, config.channelLabels[index]);
            !added)
        {
            return Core::failure(added.error());
        }
        if (Core::Status added = addTextBytes(
                budget, config.channelAccessibleNames[index]);
            !added)
        {
            return Core::failure(added.error());
        }
        if (Core::Status added = addTextBytes(
                budget, state.channelTexts[index].view());
            !added)
        {
            return Core::failure(added.error());
        }
    }
    return budget;
}

Core::Result<UINumberFieldParts>
UIContext::buildNumberField(UINodeId parent,
                            const UINumberFieldConfig& config)
{
    return buildNumberFieldImpl(*this, parent, config, productTheme());
}

Core::Result<UICollapsibleSectionParts>
UIContext::buildCollapsibleSection(
    UINodeId parent, const UICollapsibleSectionConfig& config)
{
    const auto initializeToggle =
        [this](UIElementBuildTransaction& transaction, UINodeId header,
               bool expanded) -> Core::Status {
        return setCheckedFromUpdater(transaction.m_updaterRoot, header, expanded);
    };
    return buildCollapsibleSectionImpl(*this, parent, config, productTheme(),
                                       initializeToggle);
}

Core::Result<UIColorFieldParts>
UIContext::buildColorField(UINodeId parent, const UIColorFieldConfig& config)
{
    return buildColorFieldImpl(*this, parent, config, productTheme());
}

Core::Result<UIColorPickerParts>
UIContext::buildColorPicker(UINodeId parent, const UIColorPickerConfig& config)
{
    const auto initializeRange =
        [this](UIElementBuildTransaction& transaction, UINodeId slider,
               float value) -> Core::Status {
        if (Core::Status range = setSliderRangeFromUpdater(
                transaction.m_updaterRoot, slider, 0.0F, 255.0F, 1.0F);
            !range)
        {
            return range;
        }
        return setSliderValueFromUpdater(transaction.m_updaterRoot, slider,
                                         value);
    };
    return buildColorPickerImpl(*this, parent, config, productTheme(),
                                initializeRange);
}

Core::Result<UINumberFieldParts>
UITreeUpdater::buildNumberField(UINodeId parent,
                                const UINumberFieldConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildNumberFieldImpl(*this, parent, config, m_context->productTheme());
}

Core::Result<UICollapsibleSectionParts>
UITreeUpdater::buildCollapsibleSection(
    UINodeId parent, const UICollapsibleSectionConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    const auto initializeToggle =
        [this](UIElementBuildTransaction&, UINodeId header,
               bool expanded) -> Core::Status {
        return setChecked(header, expanded);
    };
    return buildCollapsibleSectionImpl(*this, parent, config,
                                       m_context->productTheme(),
                                       initializeToggle);
}

Core::Result<UIColorFieldParts>
UITreeUpdater::buildColorField(UINodeId parent,
                               const UIColorFieldConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    return buildColorFieldImpl(*this, parent, config, m_context->productTheme());
}

Core::Result<UIColorPickerParts>
UITreeUpdater::buildColorPicker(UINodeId parent,
                                const UIColorPickerConfig& config)
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext,
                             "UI tree updater is not bound to a context");
    }
    const auto initializeRange =
        [this](UIElementBuildTransaction&, UINodeId slider,
               float value) -> Core::Status {
        if (Core::Status range =
                setSliderRange(slider, 0.0F, 255.0F, 1.0F);
            !range)
        {
            return range;
        }
        return setSliderValue(slider, value);
    };
    return buildColorPickerImpl(*this, parent, config,
                                m_context->productTheme(), initializeRange);
}

} // namespace Tina::UI
