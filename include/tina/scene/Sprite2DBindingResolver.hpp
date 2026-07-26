#pragma once

#include <tina/asset/AssetBindingResolver.hpp>

namespace Tina::Scene {

// Borrowed, allocation-free seam from a weak Sprite AssetHandle to the current
// backend-neutral render binding key. The caller keeps the callback and userData
// valid for one extraction call; Scene retains neither. The callback is expected
// to validate handle identity, asset kind, and binding readiness without retaining
// pointers or references. Returning 0 means unresolved.
using Sprite2DBindingResolver = Asset::AssetBindingResolver;

} // namespace Tina::Scene
