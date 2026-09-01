#pragma once

#include <tina/core/base/Types.hpp>

#include <array>
#include <optional>
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

// Declarative value filter for read-only queries.
//
// An unset field does not participate. This is why the fields are optional rather
// than plain values with a sentinel: 0 is a legal value for all three
// (NoEntityTag, DefaultEntityLayer, NoEntityGroup), so "match tag 0" and "do not
// filter on tag" have to be expressible separately.
//
// Filtering is limited to these three dimensions on purpose. It keeps queries
// declarative — reusable by contains() and empty(), which a caller-supplied lambda
// could not be — without turning World into a registry of arbitrary predicates.
struct EntityMetadataFilter final {
    std::optional<EntityTag> tag{};
    std::optional<EntityLayer> layer{};
    std::optional<EntityGroup> group{};

    [[nodiscard]] constexpr bool filtersNothing() const noexcept
    {
        return !tag.has_value() && !layer.has_value() && !group.has_value();
    }

    friend bool operator==(const EntityMetadataFilter&, const EntityMetadataFilter&) = default;
};

// Every set field must match. A default filter accepts every entity.
[[nodiscard]] constexpr bool matchesEntityMetadataFilter(
    const EntityMetadata& metadata,
    const EntityMetadataFilter& filter) noexcept
{
    if (filter.tag.has_value() && metadata.tag != *filter.tag) {
        return false;
    }
    if (filter.layer.has_value() && metadata.layer != *filter.layer) {
        return false;
    }
    if (filter.group.has_value() && metadata.group != *filter.group) {
        return false;
    }
    return true;
}

} // namespace Tina::Scene
