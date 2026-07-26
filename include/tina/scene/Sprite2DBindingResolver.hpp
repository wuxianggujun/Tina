#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>

namespace Tina::Scene {

// Borrowed, allocation-free seam from a weak Sprite AssetHandle to the current
// backend-neutral render binding key. The caller keeps the callback and userData
// valid for one extraction call; Scene retains neither. The callback is expected
// to validate handle identity, asset kind, and binding readiness without retaining
// pointers or references. Returning 0 means unresolved.
struct Sprite2DBindingResolver final {
    using ResolveFn = Core::u32 (*)(void* userData, Asset::AssetHandle sprite) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return resolve != nullptr;
    }

    [[nodiscard]] Core::u32 operator()(Asset::AssetHandle sprite) const noexcept
    {
        return resolve == nullptr ? 0U : resolve(userData, sprite);
    }
};

} // namespace Tina::Scene
