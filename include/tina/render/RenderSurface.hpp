#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>
#include <limits>

namespace Tina::Render {

// Backend-neutral identity copied from the internal Platform surface registry.
// This is deliberately not a native window handle and cannot be decoded into one.
struct RenderSurfaceId final {
    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();

    u32 owner = 0;
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return owner != 0 && index != InvalidIndex && generation != 0;
    }

    auto operator<=>(const RenderSurfaceId&) const = default;
};

struct RenderSurfaceExtent final {
    u32 width = 0;
    u32 height = 0;

    auto operator<=>(const RenderSurfaceExtent&) const = default;
};

struct RenderSurfaceContentScale final {
    float x = 1.0F;
    float y = 1.0F;

    auto operator<=>(const RenderSurfaceContentScale&) const = default;
};

enum class RenderSurfaceAvailability : u8 {
    Active,
    Suspended,
};

struct RenderSurfaceState final {
    RenderSurfaceId surface{};
    RenderSurfaceExtent framebufferExtent{};
    RenderSurfaceContentScale contentScale{};
    u64 sourceMetricsRevision = 0;
    u64 surfaceRevision = 0;
    // Bumped only when the *native* window behind this surface is replaced -- Android
    // destroys its ANativeWindow on background and hands back a different one on
    // resume. Orthogonal to surfaceRevision, which tracks geometry and availability.
    //
    // It is deliberately not folded into Suspended: that state promises the resources
    // are still valid and only presentation is paused, whereas a new native window
    // means the backbuffer's surface and swapchain must be rebuilt. Conflating them
    // would leave the engine holding a backbuffer attached to a window that is gone
    // (ADR 0034).
    //
    // Starts at 1 so the first committed state is already a valid binding; 0 means
    // "no binding published yet" and is refused by validation.
    u64 nativeBindingRevision = 1;
    RenderSurfaceAvailability availability = RenderSurfaceAvailability::Suspended;
};

} // namespace Tina::Render
