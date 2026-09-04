#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <cstddef>
#include <vector>

namespace Tina::Sample3D {

inline constexpr Core::u16 ProductEnvironmentDiffuseFaceSize = 2;
inline constexpr Core::u16 ProductEnvironmentSpecularFaceSize = 4;
inline constexpr Core::u16 ProductEnvironmentSpecularMipCount = 3;
inline constexpr Core::u16 ProductEnvironmentBrdfSize = 4;
inline constexpr float ProductEnvironmentIntensity = 0.85F;
inline constexpr float ProductEnvironmentRotationRadians = 0.35F;

[[nodiscard]] Core::AssetId productEnvironmentMapAssetId() noexcept;

[[nodiscard]] Core::Result<std::vector<std::byte>> makeProductEnvironmentMapPayload();

} // namespace Tina::Sample3D
