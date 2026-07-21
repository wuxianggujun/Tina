#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::Render {

// Locked value produced once at Action Mapping for an unconsumed pointer
// transition. Consumers must not re-project it with a later Camera/Surface.
struct WorldPointerSample final {
    float worldX = 0.0F;
    float worldY = 0.0F;
    u64 cameraRevision = 0;
    u64 surfaceRevision = 0;
    u64 inputSequence = 0;
    u64 stableCameraKey = 0;
    bool hit = false;

    auto operator<=>(const WorldPointerSample&) const = default;
};

} // namespace Tina::Render
