#pragma once

#include "UIDataGridStateStorage.hpp"
#include "UIGridControlValidation.hpp"
#include "UIVirtualGridViewStateStorage.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>
#include <optional>

namespace Tina::UI::Detail {

enum class UIGridCommandNavigationError : u8 {
    InvalidCommand,
    InvalidGeometry,
    LogicalShapeUnavailable,
};

struct UIVirtualGridViewNavigationPlan final {
    bool consumed = false;
    bool activateCurrentSelection = false;
    bool hasTarget = false;
    bool targetChanged = false;
    u64 targetIndex = 0;
    u64 targetRow = 0;
    u32 targetColumn = 0;
};

struct UIDataGridNavigationPlan final {
    bool consumed = false;
    bool activateCurrentSelection = false;
    bool hasTarget = false;
    bool targetChanged = false;
    u64 targetRow = 0;
    u32 targetColumn = 0;
};

namespace UIGridCommandNavigationDetail {

[[nodiscard]] constexpr u64 saturatingAdd(u64 left, u64 right) noexcept
{
    return right > (std::numeric_limits<u64>::max)() - left
               ? (std::numeric_limits<u64>::max)()
               : left + right;
}

[[nodiscard]] inline std::expected<u64, UIGridCommandNavigationError>
resolvePageRows(float viewportHeight, float rowHeight, float rowGap) noexcept
{
    if (!std::isfinite(viewportHeight) || !std::isfinite(rowHeight) ||
        !std::isfinite(rowGap) || viewportHeight < 0.0F || rowHeight <= 0.0F ||
        rowGap < 0.0F)
    {
        return std::unexpected(
            UIGridCommandNavigationError::InvalidGeometry);
    }

    const double rowStride =
        static_cast<double>(rowHeight) + static_cast<double>(rowGap);
    const double rows64 = std::floor(
        (static_cast<double>(viewportHeight) + static_cast<double>(rowGap)) /
        rowStride);
    if (rows64 >= static_cast<double>((std::numeric_limits<u64>::max)()))
    {
        return (std::numeric_limits<u64>::max)();
    }
    return static_cast<u64>((std::max)(1.0, rows64));
}

[[nodiscard]] constexpr u64 resolveVirtualGridRowCount(
    u64 logicalItemCount, u32 logicalColumnCount) noexcept
{
    return logicalItemCount == 0 || logicalColumnCount == 0
               ? 0
               : 1 + ((logicalItemCount - 1) / logicalColumnCount);
}

[[nodiscard]] constexpr std::optional<u64> resolveVirtualGridIndexForRow(
    u64 logicalItemCount, u32 logicalColumnCount, u64 targetRow,
    u32 requestedColumn) noexcept
{
    if (logicalItemCount == 0 || logicalColumnCount == 0 ||
        targetRow > (logicalItemCount - 1) / logicalColumnCount)
    {
        return std::nullopt;
    }
    // The row bound above proves this multiplication cannot exceed the last
    // logical item index, even when logicalItemCount is u64::max().
    const u64 rowStart = targetRow * logicalColumnCount;
    const u64 remaining = logicalItemCount - rowStart;
    const u32 rowItemCount = remaining < logicalColumnCount
                                 ? static_cast<u32>(remaining)
                                 : logicalColumnCount;
    const u32 targetColumn =
        (std::min)(requestedColumn, rowItemCount - 1U);
    return rowStart + targetColumn;
}

} // namespace UIGridCommandNavigationDetail

[[nodiscard]] inline std::expected<
    UIVirtualGridViewNavigationPlan, UIGridCommandNavigationError>
resolveVirtualGridViewCommandNavigation(
    UIVirtualGridViewCommand command, const VirtualGridViewState& state,
    u64 logicalItemCount) noexcept
{
    if (!isValidVirtualGridViewCommand(command))
    {
        return std::unexpected(UIGridCommandNavigationError::InvalidCommand);
    }

    UIVirtualGridViewNavigationPlan plan{.consumed = true};
    u32 logicalColumnCount = state.committedMetrics.logicalColumnCount;
    if (logicalItemCount != 0 && logicalItemCount < logicalColumnCount)
    {
        logicalColumnCount = static_cast<u32>(logicalItemCount);
    }
    const bool hasCurrentSelection =
        isVirtualGridViewSelectionValid(
            state.selection, logicalItemCount, logicalColumnCount) &&
        state.selection.hasValue();
    if (command == UIVirtualGridViewCommand::Activate)
    {
        plan.activateCurrentSelection = hasCurrentSelection;
        return plan;
    }
    if (logicalItemCount == 0)
    {
        return plan;
    }
    if (logicalColumnCount == 0)
    {
        return std::unexpected(
            UIGridCommandNavigationError::LogicalShapeUnavailable);
    }

    const u64 logicalRowCount =
        UIGridCommandNavigationDetail::resolveVirtualGridRowCount(
            logicalItemCount, logicalColumnCount);
    u64 targetIndex = 0;
    if (!hasCurrentSelection)
    {
        targetIndex = command == UIVirtualGridViewCommand::LastItem
                          ? logicalItemCount - 1
                          : 0;
    }
    else
    {
        const u64 currentIndex = state.selection.logicalIndex;
        const u64 currentRow = currentIndex / logicalColumnCount;
        const u32 currentColumn = static_cast<u32>(
            currentIndex % logicalColumnCount);
        u64 targetRow = currentRow;
        switch (command)
        {
        case UIVirtualGridViewCommand::PreviousItem:
            targetIndex = currentIndex == 0 ? 0 : currentIndex - 1;
            break;
        case UIVirtualGridViewCommand::NextItem:
            targetIndex = (std::min)(
                logicalItemCount - 1,
                UIGridCommandNavigationDetail::saturatingAdd(
                    currentIndex, u64{1}));
            break;
        case UIVirtualGridViewCommand::PreviousRow:
            targetRow = currentRow == 0 ? 0 : currentRow - 1;
            if (const auto resolvedIndex =
                    UIGridCommandNavigationDetail::resolveVirtualGridIndexForRow(
                    logicalItemCount, logicalColumnCount, targetRow,
                    currentColumn))
            {
                targetIndex = *resolvedIndex;
            } else
            {
                return std::unexpected(
                    UIGridCommandNavigationError::LogicalShapeUnavailable);
            }
            break;
        case UIVirtualGridViewCommand::NextRow:
            targetRow = (std::min)(
                logicalRowCount - 1,
                UIGridCommandNavigationDetail::saturatingAdd(
                    currentRow, u64{1}));
            if (const auto resolvedIndex =
                    UIGridCommandNavigationDetail::resolveVirtualGridIndexForRow(
                    logicalItemCount, logicalColumnCount, targetRow,
                    currentColumn))
            {
                targetIndex = *resolvedIndex;
            } else
            {
                return std::unexpected(
                    UIGridCommandNavigationError::LogicalShapeUnavailable);
            }
            break;
        case UIVirtualGridViewCommand::PreviousPage:
        case UIVirtualGridViewCommand::NextPage: {
            const auto pageRows =
                UIGridCommandNavigationDetail::resolvePageRows(
                    state.committedMetrics.viewportSize.height,
                    state.style.itemHeight, state.style.rowGap);
            if (!pageRows)
            {
                return std::unexpected(pageRows.error());
            }
            if (command == UIVirtualGridViewCommand::PreviousPage)
            {
                targetRow = currentRow > *pageRows
                                ? currentRow - *pageRows
                                : 0;
            }
            else
            {
                targetRow = (std::min)(
                    logicalRowCount - 1,
                    UIGridCommandNavigationDetail::saturatingAdd(
                        currentRow, *pageRows));
            }
            if (const auto resolvedIndex =
                    UIGridCommandNavigationDetail::resolveVirtualGridIndexForRow(
                    logicalItemCount, logicalColumnCount, targetRow,
                    currentColumn))
            {
                targetIndex = *resolvedIndex;
            } else
            {
                return std::unexpected(
                    UIGridCommandNavigationError::LogicalShapeUnavailable);
            }
            break;
        }
        case UIVirtualGridViewCommand::FirstItem:
            targetIndex = 0;
            break;
        case UIVirtualGridViewCommand::LastItem:
            targetIndex = logicalItemCount - 1;
            break;
        case UIVirtualGridViewCommand::Activate:
            break;
        }
    }

    plan.hasTarget = true;
    plan.targetChanged =
        !hasCurrentSelection || state.selection.logicalIndex != targetIndex;
    plan.targetIndex = targetIndex;
    plan.targetRow = targetIndex / logicalColumnCount;
    plan.targetColumn = static_cast<u32>(
        targetIndex % logicalColumnCount);
    return plan;
}

[[nodiscard]] inline std::expected<
    UIDataGridNavigationPlan, UIGridCommandNavigationError>
resolveDataGridCommandNavigation(
    UIDataGridCommand command, const DataGridState& state,
    u64 logicalRowCount, u32 logicalColumnCount) noexcept
{
    if (!isValidDataGridCommand(command))
    {
        return std::unexpected(UIGridCommandNavigationError::InvalidCommand);
    }

    UIDataGridNavigationPlan plan{.consumed = true};
    const bool hasCurrentSelection =
        isDataGridSelectionValid(
            state.selection, logicalRowCount, logicalColumnCount) &&
        state.selection.hasValue();
    if (command == UIDataGridCommand::Activate)
    {
        plan.activateCurrentSelection = hasCurrentSelection;
        return plan;
    }
    if (logicalRowCount == 0 || logicalColumnCount == 0)
    {
        return plan;
    }

    u64 targetRow = 0;
    u32 targetColumn = 0;
    if (!hasCurrentSelection)
    {
        if (command == UIDataGridCommand::LastCell)
        {
            targetRow = logicalRowCount - 1;
            targetColumn = logicalColumnCount - 1;
        }
    }
    else
    {
        targetRow = state.selection.logicalRow;
        targetColumn = state.selection.logicalColumn;
        switch (command)
        {
        case UIDataGridCommand::PreviousColumn:
            targetColumn = targetColumn == 0 ? 0 : targetColumn - 1;
            break;
        case UIDataGridCommand::NextColumn:
            targetColumn = targetColumn == logicalColumnCount - 1
                               ? targetColumn
                               : targetColumn + 1U;
            break;
        case UIDataGridCommand::PreviousRow:
            targetRow = targetRow == 0 ? 0 : targetRow - 1;
            break;
        case UIDataGridCommand::NextRow:
            targetRow = (std::min)(
                logicalRowCount - 1,
                UIGridCommandNavigationDetail::saturatingAdd(
                    targetRow, u64{1}));
            break;
        case UIDataGridCommand::PreviousPage:
        case UIDataGridCommand::NextPage: {
            const auto pageRows =
                UIGridCommandNavigationDetail::resolvePageRows(
                    state.committedMetrics.viewportSize.height,
                    state.style.rowHeight, 0.0F);
            if (!pageRows)
            {
                return std::unexpected(pageRows.error());
            }
            if (command == UIDataGridCommand::PreviousPage)
            {
                targetRow = targetRow > *pageRows
                                ? targetRow - *pageRows
                                : 0;
            }
            else
            {
                targetRow = (std::min)(
                    logicalRowCount - 1,
                    UIGridCommandNavigationDetail::saturatingAdd(
                        targetRow, *pageRows));
            }
            break;
        }
        case UIDataGridCommand::FirstCell:
            targetRow = 0;
            targetColumn = 0;
            break;
        case UIDataGridCommand::LastCell:
            targetRow = logicalRowCount - 1;
            targetColumn = logicalColumnCount - 1;
            break;
        case UIDataGridCommand::Activate:
            break;
        }
    }

    plan.hasTarget = true;
    plan.targetChanged =
        !hasCurrentSelection || state.selection.logicalRow != targetRow ||
        state.selection.logicalColumn != targetColumn;
    plan.targetRow = targetRow;
    plan.targetColumn = targetColumn;
    return plan;
}

} // namespace Tina::UI::Detail
