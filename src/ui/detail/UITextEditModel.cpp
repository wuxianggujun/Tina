#include "UITextEditModel.hpp"
#include "UIGraphemeBreak.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::UI::Detail {
namespace {

constexpr u32 InvalidCodepoint = (std::numeric_limits<u32>::max)();

[[nodiscard]] bool utf8UnitLength(std::string_view text, usize offset, usize& length) noexcept
{
    if (offset >= text.size())
    {
        return false;
    }
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first <= 0x7FU)
    {
        length = 1;
        return true;
    }
    if ((first & 0xE0U) == 0xC0U)
    {
        length = 2;
    }
    else if ((first & 0xF0U) == 0xE0U)
    {
        length = 3;
    }
    else if ((first & 0xF8U) == 0xF0U)
    {
        length = 4;
    }
    else
    {
        return false;
    }
    if (length > text.size() - offset)
    {
        return false;
    }
    for (usize index = 1; index < length; ++index)
    {
        if ((static_cast<unsigned char>(text[offset + index]) & 0xC0U) != 0x80U)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float resolvedAdvance(usize glyphIndex, float fallbackAdvance,
                                    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    if (glyphIndex >= glyphs.size())
    {
        return fallbackAdvance;
    }
    const float advance = glyphs[glyphIndex].advance;
    return std::isfinite(advance) && advance >= 0.0F
               ? advance
               : fallbackAdvance;
}

} // namespace

bool containsLineBreak(std::string_view text) noexcept
{
    return text.find('\r') != std::string_view::npos ||
           text.find('\n') != std::string_view::npos;
}

usize utf8ByteOffsetForCodepoint(std::string_view text, u32 codepointOffset) noexcept
{
    usize byteOffset = 0;
    u32 codepoint = 0;
    while (byteOffset < text.size() && codepoint < codepointOffset)
    {
        usize unitLength = 0;
        if (!utf8UnitLength(text, byteOffset, unitLength))
        {
            return text.size();
        }
        byteOffset += unitLength;
        ++codepoint;
    }
    return (std::min)(byteOffset, text.size());
}

bool buildTextEditVisualLayout(
    std::string_view text, float viewportWidth, float viewportHeight, float lineHeight,
    float fallbackAdvance, UITextEditWrapMode wrapMode,
    std::span<const UITextGlyphRaster> glyphs, std::span<UITextEditVisualLine> output,
    UITextEditVisualLayout& result) noexcept
{
    result = {};
    if (!(std::isfinite(viewportWidth) && viewportWidth >= 0.0F &&
          std::isfinite(viewportHeight) && viewportHeight >= 0.0F &&
          std::isfinite(lineHeight) && lineHeight > 0.0F))
    {
        return false;
    }
    if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F) || output.empty())
    {
        return false;
    }

    u32 lineBegin = 0;
    u32 lineBeginGlyphIndex = 0;
    u32 glyphIndex = 0;
    float lineWidth = 0.0F;
    u32 lineCount = 0;
    const auto appendLine = [&](u32 end, u32 hardBreak, float width) noexcept -> bool {
        if (lineCount >= output.size())
        {
            return false;
        }
        const float top = static_cast<float>(lineCount) * lineHeight;
        if (!std::isfinite(top) || !std::isfinite(width))
        {
            return false;
        }
        output[lineCount++] = UITextEditVisualLine{
            .beginCodepoint = lineBegin,
            .endCodepoint = end,
            .beginGlyphIndex = lineBeginGlyphIndex,
            .hardBreakCodepoint = hardBreak,
            .width = width,
            .top = top,
        };
        return true;
    };

    usize clusterByteOffset = 0;
    u32 clusterCodepointOffset = 0;
    while (clusterByteOffset < text.size())
    {
        UIGraphemeCluster cluster{};
        if (!nextGraphemeCluster(
                text, clusterByteOffset, clusterCodepointOffset, cluster))
        {
            return false;
        }
        const bool hardBreak =
            cluster.endCodepoint == cluster.beginCodepoint + 1U &&
            cluster.endByte == cluster.beginByte + 1U &&
            text[cluster.beginByte] == '\n';
        if (hardBreak)
        {
            if (!appendLine(cluster.beginCodepoint, cluster.beginCodepoint, lineWidth))
            {
                return false;
            }
            lineBegin = cluster.endCodepoint;
            lineBeginGlyphIndex = glyphIndex;
            lineWidth = 0.0F;
            continue;
        }

        const u32 clusterBeginGlyphIndex = glyphIndex;
        float clusterAdvance = 0.0F;
        for (u32 codepoint = cluster.beginCodepoint;
             codepoint < cluster.endCodepoint; ++codepoint)
        {
            const float advance =
                resolvedAdvance(clusterBeginGlyphIndex + (codepoint - cluster.beginCodepoint),
                                fallbackAdvance, glyphs);
            if (!(std::isfinite(advance) && advance >= 0.0F) ||
                !std::isfinite(clusterAdvance + advance))
            {
                return false;
            }
            clusterAdvance += advance;
        }
        const float nextWidth = lineWidth + clusterAdvance;
        if (!std::isfinite(nextWidth))
        {
            return false;
        }
        if (wrapMode == UITextEditWrapMode::SoftWrap && lineWidth > 0.0F && nextWidth > viewportWidth)
        {
            if (!appendLine(cluster.beginCodepoint, InvalidCodepoint, lineWidth))
            {
                return false;
            }
            lineBegin = cluster.beginCodepoint;
            lineBeginGlyphIndex = clusterBeginGlyphIndex;
            lineWidth = clusterAdvance;
        }
        else
        {
            lineWidth = nextWidth;
        }
        glyphIndex += cluster.endCodepoint - cluster.beginCodepoint;
    }

    if (!appendLine(clusterCodepointOffset, InvalidCodepoint, lineWidth))
    {
        return false;
    }
    const float contentHeight = static_cast<float>(lineCount) * lineHeight;
    if (!std::isfinite(contentHeight))
    {
        return false;
    }
    result = UITextEditVisualLayout{
        .lineCount = lineCount,
        .lineHeight = lineHeight,
        .contentHeight = contentHeight,
        .maximumScrollY = (std::max)(0.0F, contentHeight - viewportHeight),
    };
    return std::isfinite(result.maximumScrollY);
}

bool isTextEditSoftWrapBoundary(
    std::span<const UITextEditVisualLine> lines, usize lineIndex,
    u32 codepoint) noexcept
{
    if (lineIndex >= lines.size())
    {
        return false;
    }
    const UITextEditVisualLine& line = lines[lineIndex];
    return codepoint == line.endCodepoint &&
           line.hardBreakCodepoint == InvalidCodepoint &&
           lineIndex + 1U < lines.size() &&
           lines[lineIndex + 1U].beginCodepoint == codepoint;
}

UITextEditVisualHit textEditHitFromVisualPosition(
    std::string_view text, float relativeX, float relativeY, float scrollY,
    const UITextEditVisualLayout& layout,
    std::span<const UITextEditVisualLine> lines, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    if (layout.lineCount == 0 || lines.size() < layout.lineCount ||
        !(std::isfinite(relativeY) && std::isfinite(scrollY) &&
          std::isfinite(layout.lineHeight) && layout.lineHeight > 0.0F))
    {
        return {};
    }
    const float contentY = relativeY + scrollY;
    u32 row = 0;
    if (std::isfinite(contentY))
    {
        const float rowValue = std::floor(contentY / layout.lineHeight);
        row = rowValue <= 0.0F ? 0U : static_cast<u32>((std::min)(
            rowValue, static_cast<float>(layout.lineCount - 1U)));
    }
    else if (contentY > 0.0F)
    {
        row = layout.lineCount - 1U;
    }
    const UITextEditVisualLine& line = lines[row];
    if (line.beginCodepoint > line.endCodepoint)
    {
        return {};
    }
    const u32 count = line.endCodepoint - line.beginCodepoint;
    if (count == 0)
    {
        return {.codepoint = line.beginCodepoint};
    }
    const usize lineBeginGlyph = line.beginGlyphIndex;
    const std::span<const UITextGlyphRaster> lineGlyphs =
        lineBeginGlyph <= glyphs.size() && count <= glyphs.size() - lineBeginGlyph
            ? glyphs.subspan(lineBeginGlyph, count)
            : std::span<const UITextGlyphRaster>{};
    const usize lineBeginByte =
        utf8ByteOffsetForCodepoint(text, line.beginCodepoint);
    const usize lineEndByte =
        utf8ByteOffsetForCodepoint(text, line.endCodepoint);
    if (lineBeginByte > lineEndByte || lineEndByte > text.size())
    {
        return {.codepoint = line.beginCodepoint};
    }
    const std::string_view lineText =
        text.substr(lineBeginByte, lineEndByte - lineBeginByte);
    const u32 codepoint = line.beginCodepoint +
                          textEditCodepointFromHorizontalPosition(
                              lineText, relativeX, fallbackAdvance, lineGlyphs);
    return {
        .codepoint = codepoint,
        .affinity = isTextEditSoftWrapBoundary(
                        lines.first(layout.lineCount), row, codepoint)
                        ? UITextEditCaretAffinity::Upstream
                        : UITextEditCaretAffinity::Downstream,
    };
}

u32 textEditCodepointFromHorizontalPosition(
    std::string_view text, float relativeX, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    if (text.empty() || !(relativeX > 0.0F))
    {
        return 0;
    }
    if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F))
    {
        fallbackAdvance = 1.0F;
    }

    float cursorX = 0.0F;
    usize clusterByteOffset = 0;
    u32 clusterCodepointOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(
        text, clusterByteOffset, clusterCodepointOffset, cluster))
    {
        float clusterAdvance = 0.0F;
        for (u32 codepoint = cluster.beginCodepoint;
             codepoint < cluster.endCodepoint; ++codepoint)
        {
            const float glyphAdvance =
                resolvedAdvance(codepoint, fallbackAdvance, glyphs);
            if (!std::isfinite(clusterAdvance + glyphAdvance))
            {
                return cluster.beginCodepoint;
            }
            clusterAdvance += glyphAdvance;
        }
        const float midpoint = cursorX + clusterAdvance * 0.5F;
        if (!std::isfinite(midpoint) || relativeX < midpoint)
        {
            return cluster.beginCodepoint;
        }
        cursorX += clusterAdvance;
        if (!std::isfinite(cursorX))
        {
            return cluster.endCodepoint;
        }
    }
    return clusterCodepointOffset;
}

std::optional<UITextEditCommandPlan> planTextEditVisualCommand(
    std::string_view text, UITextSelection currentSelection, UITextEditCommand command,
    bool extendSelection,
    std::span<const UITextEditVisualLine> lines, UITextEditCaretAffinity caretAffinity,
    std::optional<float> preferredX,
    float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    if (lines.empty() || (command != UITextEditCommand::MoveLeft && command != UITextEditCommand::MoveRight &&
                          command != UITextEditCommand::MoveUp && command != UITextEditCommand::MoveDown &&
                          command != UITextEditCommand::MoveHome && command != UITextEditCommand::MoveEnd) ||
        !isGraphemeBoundary(text, currentSelection.anchorCodepoint) ||
        !isGraphemeBoundary(text, currentSelection.caretCodepoint))
    {
        return std::nullopt;
    }
    const u32 selectionBegin = (std::min)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);
    const u32 selectionEnd = (std::max)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);
    UITextEditCommandPlan plan{.nextSelection = currentSelection};
    const auto applyCaret = [&](u32 caret) noexcept {
        plan.nextSelection.caretCodepoint = caret;
        if (!extendSelection)
        {
            plan.nextSelection.anchorCodepoint = caret;
        }
    };
    const auto setBoundaryAffinity = [&](u32 caret, UITextEditCaretAffinity boundaryAffinity) noexcept {
        plan.nextCaretAffinity = UITextEditCaretAffinity::Downstream;
        if (boundaryAffinity != UITextEditCaretAffinity::Upstream)
        {
            return;
        }
        for (usize index = 0; index < lines.size(); ++index)
        {
            if (isTextEditSoftWrapBoundary(lines, index, caret))
            {
                plan.nextCaretAffinity = boundaryAffinity;
                return;
            }
        }
    };
    if (!extendSelection && !currentSelection.isCollapsed())
    {
        const bool towardStart = command == UITextEditCommand::MoveLeft ||
                                 command == UITextEditCommand::MoveUp ||
                                 command == UITextEditCommand::MoveHome;
        const u32 targetCaret = towardStart ? selectionBegin : selectionEnd;
        applyCaret(targetCaret);
        setBoundaryAffinity(
            targetCaret,
            command == UITextEditCommand::MoveUp || command == UITextEditCommand::MoveRight ||
                    command == UITextEditCommand::MoveEnd
                ? UITextEditCaretAffinity::Upstream
                : UITextEditCaretAffinity::Downstream);
        return plan;
    }

    if (command == UITextEditCommand::MoveLeft || command == UITextEditCommand::MoveRight)
    {
        const bool atSharedBoundary = [&]() noexcept {
            for (usize index = 0; index < lines.size(); ++index)
            {
                if (isTextEditSoftWrapBoundary(
                        lines, index, currentSelection.caretCodepoint))
                {
                    return true;
                }
            }
            return false;
        }();
        if (!extendSelection && currentSelection.isCollapsed() && atSharedBoundary)
        {
            if (command == UITextEditCommand::MoveLeft &&
                caretAffinity == UITextEditCaretAffinity::Downstream)
            {
                plan.nextCaretAffinity = UITextEditCaretAffinity::Upstream;
                return plan;
            }
            if (command == UITextEditCommand::MoveRight &&
                caretAffinity == UITextEditCaretAffinity::Upstream)
            {
                plan.nextCaretAffinity = UITextEditCaretAffinity::Downstream;
                return plan;
            }
        }

        const u32 targetCaret = command == UITextEditCommand::MoveLeft
                                    ? previousGraphemeBoundary(
                                          text, currentSelection.caretCodepoint)
                                    : nextGraphemeBoundary(
                                          text, currentSelection.caretCodepoint);
        applyCaret(targetCaret);
        setBoundaryAffinity(
            targetCaret, command == UITextEditCommand::MoveRight
                             ? UITextEditCaretAffinity::Upstream
                             : UITextEditCaretAffinity::Downstream);
        return plan;
    }

    usize currentLine = lines.size();
    for (usize index = 0; index < lines.size(); ++index)
    {
        const UITextEditVisualLine& line = lines[index];
        if (line.beginCodepoint > line.endCodepoint)
        {
            return std::nullopt;
        }
        const bool atSoftWrapBoundary = isTextEditSoftWrapBoundary(
            lines, index, currentSelection.caretCodepoint);
        if (atSoftWrapBoundary)
        {
            if (caretAffinity == UITextEditCaretAffinity::Upstream)
            {
                currentLine = index;
                break;
            }
            continue;
        }
        if (currentSelection.caretCodepoint >= line.beginCodepoint &&
            currentSelection.caretCodepoint <= line.endCodepoint)
        {
            currentLine = index;
            break;
        }
    }
    if (currentLine == lines.size())
    {
        return std::nullopt;
    }
    if (command == UITextEditCommand::MoveHome)
    {
        applyCaret(lines[currentLine].beginCodepoint);
        plan.nextCaretAffinity = UITextEditCaretAffinity::Downstream;
        return plan;
    }
    if (command == UITextEditCommand::MoveEnd)
    {
        applyCaret(lines[currentLine].endCodepoint);
        plan.nextCaretAffinity = isTextEditSoftWrapBoundary(
                                     lines, currentLine, lines[currentLine].endCodepoint)
                                     ? UITextEditCaretAffinity::Upstream
                                     : UITextEditCaretAffinity::Downstream;
        return plan;
    }
    const usize targetLine = command == UITextEditCommand::MoveUp
                                 ? (currentLine == 0 ? 0 : currentLine - 1U)
                                 : (std::min)(currentLine + 1U, lines.size() - 1U);
    const UITextEditVisualLine& line = lines[targetLine];
    if (line.beginCodepoint > line.endCodepoint)
    {
        return std::nullopt;
    }

    if (preferredX.has_value())
    {
        if (!std::isfinite(*preferredX) || *preferredX < 0.0F)
        {
            return std::nullopt;
        }
    }
    else
    {
        const UITextEditVisualLine& caretLine = lines[currentLine];
        const u32 caretOffset = currentSelection.caretCodepoint - caretLine.beginCodepoint;
        const u32 caretLineCount = caretLine.endCodepoint - caretLine.beginCodepoint;
        const usize caretLineBeginGlyph = caretLine.beginGlyphIndex;
        const std::span<const UITextGlyphRaster> caretLineGlyphs =
            caretLineBeginGlyph <= glyphs.size() &&
                    caretLineCount <= glyphs.size() - caretLineBeginGlyph
                ? glyphs.subspan(caretLineBeginGlyph, caretLineCount)
                : std::span<const UITextGlyphRaster>{};
        float caretX = 0.0F;
        for (u32 codepoint = 0; codepoint < caretOffset; ++codepoint)
        {
            const float advance = resolvedAdvance(codepoint, fallbackAdvance, caretLineGlyphs);
            if (!(std::isfinite(advance) && advance >= 0.0F) ||
                !std::isfinite(caretX + advance))
            {
                return std::nullopt;
            }
            caretX += advance;
        }
        preferredX = caretX;
    }
    plan.updatedPreferredX = preferredX;
    const float x = *preferredX;
    const u32 count = line.endCodepoint - line.beginCodepoint;
    const usize lineBeginGlyph = line.beginGlyphIndex;
    const std::span<const UITextGlyphRaster> lineGlyphs =
        lineBeginGlyph <= glyphs.size() && count <= glyphs.size() - lineBeginGlyph
            ? glyphs.subspan(lineBeginGlyph, count)
            : std::span<const UITextGlyphRaster>{};
    const usize lineBeginByte =
        utf8ByteOffsetForCodepoint(text, line.beginCodepoint);
    const usize lineEndByte =
        utf8ByteOffsetForCodepoint(text, line.endCodepoint);
    if (lineBeginByte > lineEndByte || lineEndByte > text.size())
    {
        return std::nullopt;
    }
    const std::string_view lineText =
        text.substr(lineBeginByte, lineEndByte - lineBeginByte);
    const u32 targetCaret = line.beginCodepoint +
                            textEditCodepointFromHorizontalPosition(
                                lineText, x, fallbackAdvance, lineGlyphs);
    applyCaret(targetCaret);
    plan.nextCaretAffinity = isTextEditSoftWrapBoundary(lines, targetLine, targetCaret)
                                 ? UITextEditCaretAffinity::Upstream
                                 : UITextEditCaretAffinity::Downstream;
    return plan;
}

std::optional<UITextEditCommandPlan> planTextEditCommand(
    std::string_view text, UITextSelection currentSelection,
    UITextEditCommand command, bool extendSelection) noexcept
{
    const u32 textEnd = nextGraphemeBoundary(
        text, (std::numeric_limits<u32>::max)());
    if (currentSelection.anchorCodepoint > textEnd ||
        currentSelection.caretCodepoint > textEnd ||
        !isGraphemeBoundary(text, currentSelection.anchorCodepoint) ||
        !isGraphemeBoundary(text, currentSelection.caretCodepoint))
    {
        return std::nullopt;
    }
    const u32 selectionBegin =
        (std::min)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);
    const u32 selectionEnd =
        (std::max)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);
    UITextEditCommandPlan plan{
        .nextSelection = currentSelection,
        .deleteBeginCodepoint = selectionBegin,
        .deleteEndCodepoint = selectionEnd,
    };

    switch (command)
    {
    case UITextEditCommand::MoveLeft: {
        const u32 nextCaret = !extendSelection && !currentSelection.isCollapsed()
                                  ? selectionBegin
                                  : previousGraphemeBoundary(
                                        text, currentSelection.caretCodepoint);
        plan.nextSelection.caretCodepoint = nextCaret;
        if (!extendSelection) { plan.nextSelection.anchorCodepoint = nextCaret; }
        return plan;
    }
    case UITextEditCommand::MoveRight: {
        const u32 advancedCaret =
            nextGraphemeBoundary(text, currentSelection.caretCodepoint);
        const u32 nextCaret = !extendSelection && !currentSelection.isCollapsed() ? selectionEnd : advancedCaret;
        plan.nextSelection.caretCodepoint = nextCaret;
        if (!extendSelection) { plan.nextSelection.anchorCodepoint = nextCaret; }
        return plan;
    }
    case UITextEditCommand::MoveHome:
        plan.nextSelection.caretCodepoint = 0;
        if (!extendSelection) { plan.nextSelection.anchorCodepoint = 0; }
        return plan;
    case UITextEditCommand::MoveEnd:
        plan.nextSelection.caretCodepoint = textEnd;
        if (!extendSelection) { plan.nextSelection.anchorCodepoint = textEnd; }
        return plan;
    case UITextEditCommand::SelectAll:
        plan.nextSelection = {.anchorCodepoint = 0, .caretCodepoint = textEnd};
        return plan;
    case UITextEditCommand::Backspace:
        plan.deletesText = true;
        if (currentSelection.isCollapsed())
        {
            plan.deleteBeginCodepoint =
                previousGraphemeBoundary(text, currentSelection.caretCodepoint);
            plan.deleteEndCodepoint = currentSelection.caretCodepoint;
        }
        return plan;
    case UITextEditCommand::Delete:
        plan.deletesText = true;
        if (currentSelection.isCollapsed())
        {
            plan.deleteBeginCodepoint = currentSelection.caretCodepoint;
            plan.deleteEndCodepoint =
                nextGraphemeBoundary(text, currentSelection.caretCodepoint);
        }
        return plan;
    case UITextEditCommand::MoveUp:
    case UITextEditCommand::MoveDown:
        return plan;
    }
    return std::nullopt;
}

} // namespace Tina::UI::Detail
