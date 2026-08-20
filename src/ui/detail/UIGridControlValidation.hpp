#pragma once

#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include <cmath>
#include <expected>
#include <limits>

namespace Tina::UI::Detail {

enum class UIGridControlValidationError : u8 {
    InvalidMaterializedItemCapacity,
    InvalidColumnCapacity,
    InvalidMaterializedRowCapacity,
    FixedPoolSizeOverflow,
    InvalidGeometry,
    InvalidScrollBarVisibility,
    InvalidTextOverflow,
};

struct UIVirtualGridViewFixedPoolRequirements final {
    usize items = 0;
    usize totalNodes = 0;
};

struct UIDataGridFixedPoolRequirements final {
    usize columns = 0;
    usize rows = 0;
    usize cells = 0;
    usize totalNodes = 0;
};

[[nodiscard]] constexpr bool checkedGridSizeAdd(
    usize left, usize right, usize& result) noexcept
{
    if (right > (std::numeric_limits<usize>::max)() - left)
    {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] constexpr bool checkedGridSizeMultiply(
    usize left, usize right, usize& result) noexcept
{
    if (left != 0 && right > (std::numeric_limits<usize>::max)() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] constexpr std::expected<
    UIVirtualGridViewCreateConfig, UIGridControlValidationError>
validateVirtualGridViewCreateConfig(
    UIVirtualGridViewCreateConfig config) noexcept
{
    if (config.materializedItemCapacity == 0 ||
        config.materializedItemCapacity >
            UIVirtualGridViewCreateConfig::MaximumMaterializedItemCapacity)
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidMaterializedItemCapacity);
    }
    return config;
}

[[nodiscard]] constexpr std::expected<
    UIVirtualGridViewFixedPoolRequirements, UIGridControlValidationError>
resolveVirtualGridViewFixedPoolRequirements(
    UIVirtualGridViewCreateConfig config) noexcept
{
    const auto validated = validateVirtualGridViewCreateConfig(config);
    if (!validated)
    {
        return std::unexpected(validated.error());
    }
    const usize items = validated->materializedItemCapacity;
    usize totalNodes = 0;
    if (!checkedGridSizeAdd(usize{1}, items, totalNodes))
    {
        return std::unexpected(
            UIGridControlValidationError::FixedPoolSizeOverflow);
    }
    return UIVirtualGridViewFixedPoolRequirements{
        .items = items,
        .totalNodes = totalNodes,
    };
}

[[nodiscard]] constexpr std::expected<
    UIDataGridCreateConfig, UIGridControlValidationError>
validateDataGridCreateConfig(UIDataGridCreateConfig config) noexcept
{
    if (config.columnCapacity == 0 ||
        config.columnCapacity > UIDataGridCreateConfig::MaximumColumnCapacity)
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidColumnCapacity);
    }
    if (config.materializedRowCapacity == 0 ||
        config.materializedRowCapacity >
            UIDataGridCreateConfig::MaximumMaterializedRowCapacity)
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidMaterializedRowCapacity);
    }
    return config;
}

[[nodiscard]] constexpr std::expected<
    UIDataGridFixedPoolRequirements, UIGridControlValidationError>
resolveDataGridFixedPoolRequirements(UIDataGridCreateConfig config) noexcept
{
    const auto validated = validateDataGridCreateConfig(config);
    if (!validated)
    {
        return std::unexpected(validated.error());
    }

    const usize columns = validated->columnCapacity;
    const usize rows = validated->materializedRowCapacity;
    usize cells = 0;
    usize totalNodes = 1;
    if (!checkedGridSizeMultiply(columns, rows, cells) ||
        !checkedGridSizeAdd(totalNodes, columns, totalNodes) ||
        !checkedGridSizeAdd(totalNodes, rows, totalNodes) ||
        !checkedGridSizeAdd(totalNodes, cells, totalNodes))
    {
        return std::unexpected(
            UIGridControlValidationError::FixedPoolSizeOverflow);
    }
    return UIDataGridFixedPoolRequirements{
        .columns = columns,
        .rows = rows,
        .cells = cells,
        .totalNodes = totalNodes,
    };
}

[[nodiscard]] constexpr bool isSupportedGridScrollBarVisibility(
    UIScrollBarVisibility visibility) noexcept
{
    return visibility == UIScrollBarVisibility::Auto ||
           visibility == UIScrollBarVisibility::Always ||
           visibility == UIScrollBarVisibility::Hidden;
}

[[nodiscard]] constexpr bool isSupportedGridTextOverflow(
    UITextOverflow overflow) noexcept
{
    return overflow == UITextOverflow::Clip ||
           overflow == UITextOverflow::Ellipsis;
}

[[nodiscard]] inline std::expected<
    UIVirtualGridViewStyle, UIGridControlValidationError>
validateVirtualGridViewStyle(UIVirtualGridViewStyle style) noexcept
{
    if (!std::isfinite(style.minimumItemWidth) ||
        !std::isfinite(style.itemHeight) ||
        !std::isfinite(style.columnGap) || !std::isfinite(style.rowGap) ||
        !std::isfinite(style.wheelStep) || style.minimumItemWidth <= 0.0F ||
        style.itemHeight <= 0.0F || style.columnGap < 0.0F ||
        style.rowGap < 0.0F || style.wheelStep <= 0.0F)
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidGeometry);
    }
    if (!isSupportedGridScrollBarVisibility(style.scrollBarVisibility))
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidScrollBarVisibility);
    }
    if (!isSupportedGridTextOverflow(style.itemTextOverflow))
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidTextOverflow);
    }
    return style;
}

[[nodiscard]] inline std::expected<
    UIDataGridStyle, UIGridControlValidationError>
validateDataGridStyle(UIDataGridStyle style) noexcept
{
    if (!std::isfinite(style.columnHeaderHeight) ||
        !std::isfinite(style.rowHeight) ||
        !std::isfinite(style.wheelStep) || style.columnHeaderHeight < 0.0F ||
        style.rowHeight <= 0.0F || style.wheelStep <= 0.0F)
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidGeometry);
    }
    if (!isSupportedGridScrollBarVisibility(style.scrollBarVisibility))
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidScrollBarVisibility);
    }
    if (!isSupportedGridTextOverflow(style.headerTextOverflow) ||
        !isSupportedGridTextOverflow(style.cellTextOverflow))
    {
        return std::unexpected(
            UIGridControlValidationError::InvalidTextOverflow);
    }
    return style;
}

[[nodiscard]] constexpr bool isVirtualGridViewSelectionValid(
    const UIVirtualGridViewSelection& selection, u64 logicalItemCount,
    u32 logicalColumnCount) noexcept
{
    if (!selection.hasValue())
    {
        return selection.logicalIndex == 0 && selection.logicalRow == 0 &&
               selection.logicalColumn == 0;
    }
    if (logicalItemCount == 0 || logicalColumnCount == 0 ||
        selection.logicalIndex >= logicalItemCount)
    {
        return false;
    }
    return selection.logicalRow ==
               selection.logicalIndex / logicalColumnCount &&
           selection.logicalColumn ==
               selection.logicalIndex % logicalColumnCount;
}

[[nodiscard]] constexpr bool isDataGridSelectionValid(
    const UIDataGridSelection& selection, u64 logicalRowCount,
    u32 logicalColumnCount) noexcept
{
    const bool hasRowKey = selection.rowKey != InvalidUIDataGridRowKey;
    const bool hasColumnKey =
        selection.columnKey != InvalidUIDataGridColumnKey;
    if (!hasRowKey && !hasColumnKey)
    {
        return selection.logicalRow == 0 && selection.logicalColumn == 0;
    }
    if (!hasRowKey || !hasColumnKey)
    {
        return false;
    }
    return selection.logicalRow < logicalRowCount &&
           selection.logicalColumn < logicalColumnCount;
}

[[nodiscard]] constexpr bool isValidVirtualGridViewCommand(
    UIVirtualGridViewCommand command) noexcept
{
    switch (command)
    {
    case UIVirtualGridViewCommand::PreviousItem:
    case UIVirtualGridViewCommand::NextItem:
    case UIVirtualGridViewCommand::PreviousRow:
    case UIVirtualGridViewCommand::NextRow:
    case UIVirtualGridViewCommand::PreviousPage:
    case UIVirtualGridViewCommand::NextPage:
    case UIVirtualGridViewCommand::FirstItem:
    case UIVirtualGridViewCommand::LastItem:
    case UIVirtualGridViewCommand::Activate:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidDataGridCommand(
    UIDataGridCommand command) noexcept
{
    switch (command)
    {
    case UIDataGridCommand::PreviousColumn:
    case UIDataGridCommand::NextColumn:
    case UIDataGridCommand::PreviousRow:
    case UIDataGridCommand::NextRow:
    case UIDataGridCommand::PreviousPage:
    case UIDataGridCommand::NextPage:
    case UIDataGridCommand::FirstCell:
    case UIDataGridCommand::LastCell:
    case UIDataGridCommand::Activate:
        return true;
    }
    return false;
}

} // namespace Tina::UI::Detail
