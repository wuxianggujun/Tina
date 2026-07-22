#include <tina/ui/UIText.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::UI {
namespace {

[[nodiscard]] Core::Result<UITextMetrics> invalidText(const char* message)
{
    return Core::failure(UIErrorCode::InvalidText, message);
}

[[nodiscard]] bool isFinitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

Core::Result<UITextMetrics> measurePlaceholderText(
    std::string_view utf8,
    UITextStyle style) noexcept
{
    if (!isFinitePositive(style.logicalSize)
        || !isFinitePositive(style.advanceScale)
        || !isFinitePositive(style.lineHeightScale)) {
        return invalidText(
            "UI text style size and scales must be finite and positive");
    }

    const auto codepointCount = Core::countStrictUtf8CodepointsWithoutNul(utf8);
    if (!codepointCount.has_value()) {
        return invalidText("UI text must be strict UTF-8 without embedded NUL");
    }

    const float advance = style.logicalSize * style.advanceScale;
    const float lineHeight = style.logicalSize * style.lineHeightScale;
    if (!std::isfinite(advance) || !std::isfinite(lineHeight)) {
        return invalidText("UI text metrics overflowed finite float range");
    }

    u32 lineCount = utf8.empty() ? 0U : 1U;
    u32 currentLineCodepoints = 0;
    u32 maxLineCodepoints = 0;
    usize index = 0;
    while (index < utf8.size()) {
        const auto first = static_cast<unsigned char>(utf8[index]);
        usize unitLength = 1;
        if (first <= 0x7FU) {
            unitLength = 1;
        } else if ((first & 0xE0U) == 0xC0U) {
            unitLength = 2;
        } else if ((first & 0xF0U) == 0xE0U) {
            unitLength = 3;
        } else {
            unitLength = 4;
        }
        if (unitLength > utf8.size() - index) {
            return invalidText("UI text must be strict UTF-8 without embedded NUL");
        }

        const bool isNewline = unitLength == 1 && first == '\n';
        if (isNewline) {
            maxLineCodepoints = (std::max)(maxLineCodepoints, currentLineCodepoints);
            currentLineCodepoints = 0;
            if (lineCount == (std::numeric_limits<u32>::max)()) {
                return invalidText("UI text line count overflowed");
            }
            ++lineCount;
        } else {
            if (currentLineCodepoints == (std::numeric_limits<u32>::max)()) {
                return invalidText("UI text line codepoint count overflowed");
            }
            ++currentLineCodepoints;
        }
        index += unitLength;
    }
    maxLineCodepoints = (std::max)(maxLineCodepoints, currentLineCodepoints);

    if (utf8.empty()) {
        return UITextMetrics{
            .measuredSize = {},
            .codepointCount = 0,
            .lineCount = 0,
        };
    }

    const double width =
        static_cast<double>(maxLineCodepoints) * static_cast<double>(advance);
    const double height =
        static_cast<double>(lineCount) * static_cast<double>(lineHeight);
    if (!std::isfinite(width) || !std::isfinite(height)
        || width > static_cast<double>((std::numeric_limits<float>::max)())
        || height > static_cast<double>((std::numeric_limits<float>::max)())) {
        return invalidText("UI text metrics overflowed finite float range");
    }

    return UITextMetrics{
        .measuredSize =
            UILogicalSize{
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
            },
        .codepointCount = *codepointCount,
        .lineCount = lineCount,
    };
}

} // namespace Tina::UI
