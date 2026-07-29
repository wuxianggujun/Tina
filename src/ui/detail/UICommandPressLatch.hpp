#pragma once

#include <tina/core/base/Types.hpp>

#include <array>
#include <type_traits>

namespace Tina::UI::Detail {

template <typename Command, Command LastCommand>
class UICommandPressLatch final {
    static_assert(std::is_enum_v<Command>);

public:
    [[nodiscard]] static constexpr bool accepts(Command command) noexcept
    {
        const Underlying value = static_cast<Underlying>(command);
        if constexpr (std::is_signed_v<Underlying>)
        {
            if (value < 0)
            {
                return false;
            }
        }
        return static_cast<usize>(value) < CommandCount;
    }

    [[nodiscard]] bool isLatched(Command command) const noexcept
    {
        return accepts(command) && latched_[indexOf(command)];
    }

    void latch(Command command) noexcept
    {
        if (accepts(command))
        {
            latched_[indexOf(command)] = true;
        }
    }

    [[nodiscard]] bool release(Command command) noexcept
    {
        if (!accepts(command))
        {
            return false;
        }
        const usize index = indexOf(command);
        const bool consumed = latched_[index];
        latched_[index] = false;
        return consumed;
    }

    void clear() noexcept
    {
        latched_.fill(false);
    }

private:
    using Underlying = std::underlying_type_t<Command>;
    static constexpr usize CommandCount = static_cast<usize>(LastCommand) + 1;
    static_assert(CommandCount != 0);

    [[nodiscard]] static constexpr usize indexOf(Command command) noexcept
    {
        return static_cast<usize>(static_cast<Underlying>(command));
    }

    std::array<bool, CommandCount> latched_{};
};

} // namespace Tina::UI::Detail
