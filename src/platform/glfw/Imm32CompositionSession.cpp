#include "Imm32CompositionSession.hpp"

#include <tina/core/text/Utf8.hpp>
#include <tina/platform/PlatformErrors.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] Core::Status invalidComposition(const char* message)
{
    return Core::failure(PlatformErrorCode::InvalidFrameSnapshot, message);
}

} // namespace

Imm32CompositionSession::Imm32CompositionSession(usize preeditByteCapacity) noexcept
    : preeditByteCapacity_(
          preeditByteCapacity == 0
              ? DefaultImePreeditByteCapacity
              : (std::min)(preeditByteCapacity, DefaultImePreeditByteCapacity))
{
}

std::string_view Imm32CompositionSession::preeditUtf8() const noexcept
{
    return std::string_view(preeditBytes_.data(), preeditSize_);
}

Core::Status Imm32CompositionSession::storePreedit(std::string_view utf8) noexcept
{
    if (!Core::isStrictUtf8WithoutNul(utf8)) {
        return invalidComposition("IME preedit must be strict UTF-8 without embedded NUL");
    }
    if (utf8.size() > preeditByteCapacity_) {
        return Core::failure(
            Core::CoreErrorCode::CapacityExceeded,
            "IME preedit exceeds the fixed composition buffer capacity");
    }
    if (!utf8.empty()) {
        std::memcpy(preeditBytes_.data(), utf8.data(), utf8.size());
    }
    preeditSize_ = utf8.size();
    return Core::success();
}

Core::Result<Imm32CompositionEvent> Imm32CompositionSession::updatePreedit(
    std::string_view utf8,
    u32 cursorCodepoint) noexcept
{
    if (Core::Status status = storePreedit(utf8); !status) {
        return Core::failure(status.error());
    }
    const auto codepoints = Core::countStrictUtf8CodepointsWithoutNul(utf8);
    if (!codepoints.has_value()) {
        return Core::failure(
            invalidComposition("IME preedit must be strict UTF-8 without embedded NUL").error());
    }
    cursorCodepoint_ = (std::min)(cursorCodepoint, *codepoints);
    const TextCompositionStage stage =
        active_ ? TextCompositionStage::Updated : TextCompositionStage::Started;
    active_ = true;
    return Imm32CompositionEvent{
        .stage = stage,
        .preeditUtf8 = preeditUtf8(),
        .cursorCodepoint = cursorCodepoint_,
    };
}

std::optional<Imm32CompositionEvent> Imm32CompositionSession::end(
    std::string_view committedUtf8) noexcept
{
    if (!active_ && committedUtf8.empty()) {
        return std::nullopt;
    }
    Imm32CompositionEvent event{
        .stage = TextCompositionStage::Ended,
        .preeditUtf8 = {},
        .cursorCodepoint = 0,
        .committedUtf8 = committedUtf8,
    };
    active_ = false;
    preeditSize_ = 0;
    cursorCodepoint_ = 0;
    return event;
}

std::optional<Imm32CompositionEvent> Imm32CompositionSession::cancel() noexcept
{
    if (!active_) {
        return std::nullopt;
    }
    Imm32CompositionEvent event{
        .stage = TextCompositionStage::Cancelled,
        .preeditUtf8 = {},
        .cursorCodepoint = 0,
    };
    active_ = false;
    preeditSize_ = 0;
    cursorCodepoint_ = 0;
    return event;
}

std::optional<Imm32CompositionEvent> Imm32CompositionSession::onFocusLost() noexcept
{
    return cancel();
}

std::optional<usize> encodeUtf16ToUtf8(
    std::span<const char16_t> utf16,
    std::span<char> outUtf8) noexcept
{
    usize out = 0;
    usize index = 0;
    while (index < utf16.size()) {
        u32 codepoint = utf16[index];
        usize units = 1;
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
            if (index + 1 >= utf16.size()) {
                return std::nullopt;
            }
            const u32 low = utf16[index + 1];
            if (low < 0xDC00U || low > 0xDFFFU) {
                return std::nullopt;
            }
            codepoint = 0x10000U
                + (((codepoint - 0xD800U) << 10U) | (low - 0xDC00U));
            units = 2;
        } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
            return std::nullopt;
        }

        usize needed = 0;
        if (codepoint <= 0x7FU) {
            needed = 1;
        } else if (codepoint <= 0x7FFU) {
            needed = 2;
        } else if (codepoint <= 0xFFFFU) {
            needed = 3;
        } else if (codepoint <= 0x10FFFFU) {
            needed = 4;
        } else {
            return std::nullopt;
        }
        if (out + needed > outUtf8.size()) {
            return std::nullopt;
        }

        if (needed == 1) {
            outUtf8[out] = static_cast<char>(codepoint);
        } else if (needed == 2) {
            outUtf8[out] = static_cast<char>(0xC0U | ((codepoint >> 6U) & 0x1FU));
            outUtf8[out + 1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else if (needed == 3) {
            outUtf8[out] = static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU));
            outUtf8[out + 1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            outUtf8[out + 2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else {
            outUtf8[out] = static_cast<char>(0xF0U | ((codepoint >> 18U) & 0x07U));
            outUtf8[out + 1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
            outUtf8[out + 2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            outUtf8[out + 3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        }
        out += needed;
        index += units;
    }
    return out;
}

} // namespace Tina::Platform::Detail
