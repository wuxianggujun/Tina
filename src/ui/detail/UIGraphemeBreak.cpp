#include "UIGraphemeBreak.hpp"

#include <limits>

namespace Tina::UI::Detail {
namespace {

struct UnicodeRange final {
    u32 first = 0;
    u32 last = 0;
};

#include "UIGraphemeBreakData.inc"

constexpr UnicodeRange ExtraExtendRanges[] = {
    {0x00200CU, 0x00200CU}, // ZERO WIDTH NON-JOINER
    {0x00FF9EU, 0x00FF9FU}, // Halfwidth voiced sound marks
    {0x01F3FBU, 0x01F3FFU}, // Emoji modifiers
    {0x0E0020U, 0x0E007FU}, // Emoji tag characters
};

constexpr UnicodeRange PrependRanges[] = {
    {0x000600U, 0x000605U},
    {0x0006DDU, 0x0006DDU},
    {0x00070FU, 0x00070FU},
    {0x000890U, 0x000891U},
    {0x0008E2U, 0x0008E2U},
    {0x000D4EU, 0x000D4EU},
    {0x0110BDU, 0x0110BDU},
    {0x0110CDU, 0x0110CDU},
    {0x0111C2U, 0x0111C3U},
    {0x01193FU, 0x01193FU},
    {0x011941U, 0x011941U},
    {0x011A3AU, 0x011A3AU},
    {0x011A84U, 0x011A89U},
    {0x011D46U, 0x011D46U},
    {0x011F02U, 0x011F02U},
    {0x013430U, 0x01343FU},
};

// Frozen Extended_Pictographic subset. It covers the standardized emoji
// blocks plus the earlier BMP pictographs standardized for emoji ZWJ sequences.
constexpr UnicodeRange ExtendedPictographicRanges[] = {
    {0x0000A9U, 0x0000A9U},
    {0x0000AEU, 0x0000AEU},
    {0x00203CU, 0x002049U},
    {0x002122U, 0x002139U},
    {0x002194U, 0x002199U},
    {0x0021A9U, 0x0021AAU},
    {0x00231AU, 0x00231BU},
    {0x002328U, 0x002328U},
    {0x0023CFU, 0x0023CFU},
    {0x0023E9U, 0x0023FAU},
    {0x0024C2U, 0x0024C2U},
    {0x0025AAU, 0x0025ABU},
    {0x0025B6U, 0x0025B6U},
    {0x0025C0U, 0x0025C0U},
    {0x0025FBU, 0x0025FEU},
    {0x002600U, 0x0027BFU},
    {0x002934U, 0x002935U},
    {0x002B05U, 0x002B07U},
    {0x002B1BU, 0x002B1CU},
    {0x002B50U, 0x002B50U},
    {0x002B55U, 0x002B55U},
    {0x003030U, 0x003030U},
    {0x00303DU, 0x00303DU},
    {0x003297U, 0x003297U},
    {0x003299U, 0x003299U},
    {0x01F000U, 0x01FAFFU},
};

template <usize RangeCount>
[[nodiscard]] constexpr bool inRanges(
    u32 codepoint, const UnicodeRange (&ranges)[RangeCount]) noexcept
{
    usize low = 0;
    usize high = RangeCount;
    while (low < high)
    {
        const usize middle = low + (high - low) / 2U;
        if (codepoint < ranges[middle].first)
        {
            high = middle;
        }
        else if (codepoint > ranges[middle].last)
        {
            low = middle + 1U;
        }
        else
        {
            return true;
        }
    }
    return false;
}

enum class GraphemeBreakClass : u8 {
    Other,
    Cr,
    Lf,
    Control,
    Extend,
    Zwj,
    RegionalIndicator,
    Prepend,
    SpacingMark,
    HangulL,
    HangulV,
    HangulT,
    HangulLv,
    HangulLvt,
};

[[nodiscard]] bool decodeStrictUtf8Scalar(
    std::string_view text, usize offset, u32& codepoint, usize& unitLength) noexcept
{
    if (offset >= text.size())
    {
        return false;
    }
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first <= 0x7FU)
    {
        if (first == 0U)
        {
            return false;
        }
        codepoint = first;
        unitLength = 1;
        return true;
    }

    usize continuationCount = 0;
    u32 minimumCodepoint = 0;
    if ((first & 0xE0U) == 0xC0U)
    {
        continuationCount = 1;
        codepoint = first & 0x1FU;
        minimumCodepoint = 0x80U;
    }
    else if ((first & 0xF0U) == 0xE0U)
    {
        continuationCount = 2;
        codepoint = first & 0x0FU;
        minimumCodepoint = 0x800U;
    }
    else if ((first & 0xF8U) == 0xF0U)
    {
        continuationCount = 3;
        codepoint = first & 0x07U;
        minimumCodepoint = 0x10000U;
    }
    else
    {
        return false;
    }
    if (continuationCount > text.size() - offset - 1U)
    {
        return false;
    }
    for (usize continuation = 1; continuation <= continuationCount; ++continuation)
    {
        const auto next = static_cast<unsigned char>(text[offset + continuation]);
        if ((next & 0xC0U) != 0x80U)
        {
            return false;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if (codepoint < minimumCodepoint || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
    {
        return false;
    }
    unitLength = continuationCount + 1U;
    return true;
}

[[nodiscard]] GraphemeBreakClass classify(u32 codepoint) noexcept
{
    if (codepoint == 0x000DU) { return GraphemeBreakClass::Cr; }
    if (codepoint == 0x000AU) { return GraphemeBreakClass::Lf; }
    if (codepoint == 0x200DU) { return GraphemeBreakClass::Zwj; }
    if (codepoint >= 0x1F1E6U && codepoint <= 0x1F1FFU)
    {
        return GraphemeBreakClass::RegionalIndicator;
    }
    if ((codepoint >= 0x1100U && codepoint <= 0x115FU) ||
        (codepoint >= 0xA960U && codepoint <= 0xA97CU))
    {
        return GraphemeBreakClass::HangulL;
    }
    if ((codepoint >= 0x1160U && codepoint <= 0x11A7U) ||
        (codepoint >= 0xD7B0U && codepoint <= 0xD7C6U))
    {
        return GraphemeBreakClass::HangulV;
    }
    if ((codepoint >= 0x11A8U && codepoint <= 0x11FFU) ||
        (codepoint >= 0xD7CBU && codepoint <= 0xD7FBU))
    {
        return GraphemeBreakClass::HangulT;
    }
    if (codepoint >= 0xAC00U && codepoint <= 0xD7A3U)
    {
        return (codepoint - 0xAC00U) % 28U == 0U
                   ? GraphemeBreakClass::HangulLv
                   : GraphemeBreakClass::HangulLvt;
    }
    if (inRanges(codepoint, ExtraExtendRanges) ||
        inRanges(codepoint, UnicodeExtendRanges))
    {
        return GraphemeBreakClass::Extend;
    }
    if (inRanges(codepoint, PrependRanges))
    {
        return GraphemeBreakClass::Prepend;
    }
    if (inRanges(codepoint, UnicodeSpacingMarkRanges))
    {
        return GraphemeBreakClass::SpacingMark;
    }
    if (inRanges(codepoint, UnicodeControlRanges))
    {
        return GraphemeBreakClass::Control;
    }
    return GraphemeBreakClass::Other;
}

[[nodiscard]] bool isExtendedPictographic(u32 codepoint) noexcept
{
    return inRanges(codepoint, ExtendedPictographicRanges);
}

[[nodiscard]] constexpr bool breaksAroundControl(GraphemeBreakClass value) noexcept
{
    return value == GraphemeBreakClass::Cr || value == GraphemeBreakClass::Lf ||
           value == GraphemeBreakClass::Control;
}

[[nodiscard]] bool shouldBreak(
    GraphemeBreakClass previous, GraphemeBreakClass current,
    bool previousZwjHasExtendedPictographicPrefix, u32 regionalIndicatorRun,
    u32 currentCodepoint) noexcept
{
    if (previous == GraphemeBreakClass::Cr && current == GraphemeBreakClass::Lf)
    {
        return false;
    }
    if (breaksAroundControl(previous) || breaksAroundControl(current))
    {
        return true;
    }
    if (previous == GraphemeBreakClass::HangulL &&
        (current == GraphemeBreakClass::HangulL || current == GraphemeBreakClass::HangulV ||
         current == GraphemeBreakClass::HangulLv || current == GraphemeBreakClass::HangulLvt))
    {
        return false;
    }
    if ((previous == GraphemeBreakClass::HangulLv || previous == GraphemeBreakClass::HangulV) &&
        (current == GraphemeBreakClass::HangulV || current == GraphemeBreakClass::HangulT))
    {
        return false;
    }
    if ((previous == GraphemeBreakClass::HangulLvt || previous == GraphemeBreakClass::HangulT) &&
        current == GraphemeBreakClass::HangulT)
    {
        return false;
    }
    if (current == GraphemeBreakClass::Extend || current == GraphemeBreakClass::Zwj ||
        current == GraphemeBreakClass::SpacingMark)
    {
        return false;
    }
    if (previous == GraphemeBreakClass::Prepend)
    {
        return false;
    }
    if (previous == GraphemeBreakClass::Zwj &&
        previousZwjHasExtendedPictographicPrefix &&
        isExtendedPictographic(currentCodepoint))
    {
        return false;
    }
    if (previous == GraphemeBreakClass::RegionalIndicator &&
        current == GraphemeBreakClass::RegionalIndicator &&
        regionalIndicatorRun % 2U == 1U)
    {
        return false;
    }
    return true;
}

} // namespace

bool nextGraphemeCluster(
    std::string_view text, usize& byteOffset, u32& codepointOffset,
    UIGraphemeCluster& cluster) noexcept
{
    if (byteOffset >= text.size())
    {
        return false;
    }

    const usize beginByte = byteOffset;
    const u32 beginCodepoint = codepointOffset;
    u32 previousCodepoint = 0;
    usize previousLength = 0;
    if (!decodeStrictUtf8Scalar(text, byteOffset, previousCodepoint, previousLength))
    {
        return false;
    }
    usize cursorByte = byteOffset + previousLength;
    if (codepointOffset == (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    u32 cursorCodepoint = codepointOffset + 1U;
    GraphemeBreakClass previousClass = classify(previousCodepoint);
    u32 regionalIndicatorRun =
        previousClass == GraphemeBreakClass::RegionalIndicator ? 1U : 0U;
    bool extendedPictographicBeforeExtends =
        isExtendedPictographic(previousCodepoint);
    bool previousZwjHasExtendedPictographicPrefix = false;

    while (cursorByte < text.size())
    {
        u32 currentCodepoint = 0;
        usize currentLength = 0;
        if (!decodeStrictUtf8Scalar(text, cursorByte, currentCodepoint, currentLength))
        {
            return false;
        }
        const GraphemeBreakClass currentClass = classify(currentCodepoint);
        if (shouldBreak(previousClass, currentClass,
                        previousZwjHasExtendedPictographicPrefix,
                        regionalIndicatorRun, currentCodepoint))
        {
            break;
        }

        const bool currentZwjHasExtendedPictographicPrefix =
            currentClass == GraphemeBreakClass::Zwj &&
            extendedPictographicBeforeExtends;
        if (currentClass == GraphemeBreakClass::Extend)
        {
            // Preserve an EP Extend* prefix.
        }
        else if (currentClass == GraphemeBreakClass::Zwj)
        {
            extendedPictographicBeforeExtends = false;
        }
        else
        {
            extendedPictographicBeforeExtends =
                isExtendedPictographic(currentCodepoint);
        }

        if (currentClass == GraphemeBreakClass::RegionalIndicator)
        {
            regionalIndicatorRun =
                previousClass == GraphemeBreakClass::RegionalIndicator
                    ? regionalIndicatorRun + 1U
                    : 1U;
        }
        else
        {
            regionalIndicatorRun = 0;
        }
        previousClass = currentClass;
        previousZwjHasExtendedPictographicPrefix =
            currentZwjHasExtendedPictographicPrefix;
        cursorByte += currentLength;
        if (cursorCodepoint == (std::numeric_limits<u32>::max)())
        {
            return false;
        }
        ++cursorCodepoint;
    }

    cluster = UIGraphemeCluster{
        .beginByte = beginByte,
        .endByte = cursorByte,
        .beginCodepoint = beginCodepoint,
        .endCodepoint = cursorCodepoint,
    };
    byteOffset = cursorByte;
    codepointOffset = cursorCodepoint;
    return true;
}

bool isGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept
{
    if (codepointOffset == 0)
    {
        return true;
    }
    usize byteOffset = 0;
    u32 scalarOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, scalarOffset, cluster))
    {
        if (codepointOffset == cluster.endCodepoint)
        {
            return true;
        }
        if (codepointOffset < cluster.endCodepoint)
        {
            return false;
        }
    }
    return false;
}

u32 previousGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept
{
    if (codepointOffset == 0)
    {
        return 0;
    }
    usize byteOffset = 0;
    u32 scalarOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, scalarOffset, cluster))
    {
        if (codepointOffset <= cluster.endCodepoint)
        {
            return cluster.beginCodepoint;
        }
    }
    return scalarOffset;
}

u32 nextGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept
{
    usize byteOffset = 0;
    u32 scalarOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, scalarOffset, cluster))
    {
        if (codepointOffset < cluster.endCodepoint)
        {
            return cluster.endCodepoint;
        }
    }
    return scalarOffset;
}

u32 graphemeBoundaryAtOrAfter(
    std::string_view text, u32 codepointOffset) noexcept
{
    if (codepointOffset == 0)
    {
        return 0;
    }
    usize byteOffset = 0;
    u32 scalarOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, scalarOffset, cluster))
    {
        if (codepointOffset == cluster.beginCodepoint ||
            codepointOffset == cluster.endCodepoint)
        {
            return codepointOffset;
        }
        if (codepointOffset < cluster.endCodepoint)
        {
            return cluster.endCodepoint;
        }
    }
    return scalarOffset;
}

u32 nearestGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept
{
    if (codepointOffset == 0)
    {
        return 0;
    }
    usize byteOffset = 0;
    u32 scalarOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, scalarOffset, cluster))
    {
        if (codepointOffset == cluster.beginCodepoint ||
            codepointOffset == cluster.endCodepoint)
        {
            return codepointOffset;
        }
        if (codepointOffset < cluster.endCodepoint)
        {
            const u32 distanceToBegin =
                codepointOffset - cluster.beginCodepoint;
            const u32 distanceToEnd =
                cluster.endCodepoint - codepointOffset;
            return distanceToBegin < distanceToEnd
                       ? cluster.beginCodepoint
                       : cluster.endCodepoint;
        }
    }
    return scalarOffset;
}

} // namespace Tina::UI::Detail
