#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Asset {

struct AssetHandleTag final {};
using AssetHandleId = Core::GenerationId<AssetHandleTag>;

// Copyable weak handle. Does not keep payload alive.
struct AssetHandle final {
    AssetHandleId id{};

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return static_cast<bool>(id);
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) = default;
};

} // namespace Tina::Asset
