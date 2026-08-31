#pragma once

#include <tina/core/base/Types.hpp>

#include <array>
#include <string_view>

namespace Tina::Scene {

// Per-entity runtime metadata. These values are intentionally opaque to Scene
// so a game can define its own tag, layer, and group registries without
// widening World into a dynamic ECS. World2D adapters may copy authored names;
// tag/layer/group remain runtime-only.
using EntityTag = u32;
using EntityLayer = u32;
using EntityGroup = u32;

inline constexpr EntityTag NoEntityTag = 0;
inline constexpr EntityLayer DefaultEntityLayer = 0;
inline constexpr EntityGroup NoEntityGroup = 0;

inline constexpr usize EntityNameMaximumBytes = 63;

struct EntityName final {
    std::array<char, EntityNameMaximumBytes + 1U> bytes{};
    u8 size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{bytes.data(), size};
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size == 0;
    }

    friend bool operator==(const EntityName&, const EntityName&) = default;
};

struct EntityMetadata final {
    EntityName name{};
    EntityTag tag = NoEntityTag;
    EntityLayer layer = DefaultEntityLayer;
    EntityGroup group = NoEntityGroup;

    friend bool operator==(const EntityMetadata&, const EntityMetadata&) = default;
};

// Input form for an atomic metadata replacement. World validates and copies
// the borrowed name; it never retains the caller's string_view.
struct EntityMetadataDesc final {
    std::string_view name{};
    EntityTag tag = NoEntityTag;
    EntityLayer layer = DefaultEntityLayer;
    EntityGroup group = NoEntityGroup;
};

} // namespace Tina::Scene
