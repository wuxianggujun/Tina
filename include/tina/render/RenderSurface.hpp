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
    RenderSurfaceAvailability availability = RenderSurfaceAvailability::Suspended;
};

} // namespace Tina::Render
