#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <string_view>

namespace Tina::UI::Detail {

class UIImeCompositionState final {
public:
    static constexpr usize MaximumPreeditBytes = 512;

    [[nodiscard]] static Core::Status validateCapacity(std::string_view preeditUtf8);

    void assign(std::string_view preeditUtf8, u32 cursorCodepoint, u32 codepointCount) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::string_view preeditUtf8() const noexcept;
    [[nodiscard]] u32 cursorCodepoint() const noexcept;

private:
    std::array<char, MaximumPreeditBytes> preeditBytes_{};
    usize preeditSize_ = 0;
    u32 cursorCodepoint_ = 0;
    bool active_ = false;
};

} // namespace Tina::UI::Detail
