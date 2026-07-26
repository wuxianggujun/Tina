#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>

namespace Tina::Asset {

// Borrowed, allocation-free seam from a weak AssetHandle to a caller-defined
// backend-neutral binding key. The callback and userData are valid only for the
// current operation; this value owns neither. Returning 0 means unresolved.
struct AssetBindingResolver final {
    using ResolveFn = Core::u32 (*)(void* userData, AssetHandle asset) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return resolve != nullptr;
    }

    [[nodiscard]] Core::u32 operator()(AssetHandle asset) const noexcept
    {
        return resolve == nullptr ? 0U : resolve(userData, asset);
    }
};

} // namespace Tina::Asset
