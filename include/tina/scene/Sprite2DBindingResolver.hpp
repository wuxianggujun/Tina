#pragma once

#include <tina/asset/AssetFrameResourceResolver.hpp>

namespace Tina::Scene {

// Scene-facing name for the shared borrowed, allocation-free Asset -> packet
// resource seam. Scene retains neither the callback nor its userData.
using Sprite2DBindingResolver = Asset::AssetFrameResourceResolver;

} // namespace Tina::Scene
