#pragma once

#include <tina/platform/Input.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::Platform::Detail {

struct EncodedUtf8Codepoint final {
    std::array<char, 4> bytes{};
    u8 size = 0;

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {bytes.data(), size};
    }
};

[[nodiscard]] Key translateGlfwKey(int nativeKey) noexcept;
[[nodiscard]] std::optional<PointerButton> translateGlfwPointerButton(int nativeButton) noexcept;
[[nodiscard]] std::optional<EncodedUtf8Codepoint> encodeUtf8Codepoint(u32 codepoint) noexcept;
[[nodiscard]] bool shouldAcceptGlfwKeyAction(int nativeAction, bool wasHeld) noexcept;

} // namespace Tina::Platform::Detail
