#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/text/Utf8.hpp>

#include <array>
#include <string_view>

namespace Tina::Editor {

// One short UTF-8 label per retained revision. Labels are in-memory only; they
// are not part of the cooked/snapshot wire and do not bump any asset schema.
namespace AuthoringHistoryLimits {

inline constexpr Core::usize MaximumLabelBytes = 64;

} // namespace AuthoringHistoryLimits

struct AuthoringHistoryLabel final {
    std::array<char, AuthoringHistoryLimits::MaximumLabelBytes> storage{};
    Core::u8 size = 0;

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {storage.data(), static_cast<Core::usize>(size)};
    }
};

[[nodiscard]] inline AuthoringHistoryLabel makeAuthoringHistoryLabel(
    std::string_view text) noexcept
{
    AuthoringHistoryLabel label{};
    if (text.empty() || !Core::isStrictUtf8WithoutNul(text)) {
        return label;
    }
    Core::usize offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        Core::usize unit = 1;
        if (first >= 0xF0U) {
            unit = 4;
        } else if (first >= 0xE0U) {
            unit = 3;
        } else if (first >= 0xC0U) {
            unit = 2;
        }
        if (offset + unit > text.size() ||
            offset + unit > AuthoringHistoryLimits::MaximumLabelBytes) {
            break;
        }
        offset += unit;
    }
    if (offset == 0) {
        return label;
    }
    for (Core::usize index = 0; index < offset; ++index) {
        label.storage[index] = text[index];
    }
    label.size = static_cast<Core::u8>(offset);
    return label;
}

// The next successful commit/baseline consumes this label. A no-op or failed
// edit must clear it so a later unrelated mutation cannot inherit the name.
class AuthoringHistoryPendingLabel final {
public:
    void set(std::string_view text) noexcept
    {
        m_label = makeAuthoringHistoryLabel(text);
        m_pending = true;
    }

    void clear() noexcept
    {
        m_label = {};
        m_pending = false;
    }

    [[nodiscard]] AuthoringHistoryLabel take(std::string_view fallback) noexcept
    {
        if (m_pending) {
            const AuthoringHistoryLabel result =
                m_label.size == 0 ? makeAuthoringHistoryLabel(fallback) : m_label;
            clear();
            return result;
        }
        return makeAuthoringHistoryLabel(fallback);
    }

private:
    AuthoringHistoryLabel m_label{};
    bool m_pending = false;
};

} // namespace Tina::Editor
