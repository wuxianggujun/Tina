#pragma once

#include <tina/asset/AssetHandle.hpp>

#include <cmath>

namespace Tina::Scene {

// Binds a cooked SpriteAnimationClip asset to this entity's SpriteRenderer2D.
// The World stores only the weak clip handle plus playback intent; running an
// animator (SpriteAnimator2D) and writing frames back into the sprite stays a
// game/tool responsibility.
struct SpriteAnimationBinding2D final {
    Asset::AssetHandle clip{};
    float playbackSpeed = 1.0F;
    bool autoPlay = true;

    friend constexpr bool operator==(const SpriteAnimationBinding2D&,
                                     const SpriteAnimationBinding2D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const SpriteAnimationBinding2D& binding) noexcept
{
    return static_cast<bool>(binding.clip) && std::isfinite(binding.playbackSpeed) &&
           binding.playbackSpeed > 0.0F;
}

} // namespace Tina::Scene
