#include <tina/scene/EntityMetadata.hpp>

#include <type_traits>

static_assert(Tina::Scene::EntityNameMaximumBytes > 0);
// The World2D wire format carries names in a 64-byte slot, so the runtime cap is
// one byte less. Keeping the two in lockstep is what lets the snapshot round trip
// byte-exact instead of truncating on the way out.
static_assert(Tina::Scene::EntityNameMaximumBytes == 63U);
// The byte array is NUL-padded to the wire slot width, and the length is a separate
// field rather than derived by scanning -- so the struct is one byte wider than the
// slot it serializes into.
static_assert(sizeof(Tina::Scene::EntityName{}.bytes) == 64U);
static_assert(sizeof(Tina::Scene::EntityName) == 65U);

static_assert(std::is_trivially_copyable_v<Tina::Scene::EntityName>);
static_assert(std::is_trivially_copyable_v<Tina::Scene::EntityMetadata>);

// Zero is a meaningful value for all three dimensions, which is exactly why the
// filter below uses optional instead of treating 0 as "unset".
static_assert(Tina::Scene::NoEntityTag == 0U);
static_assert(Tina::Scene::DefaultEntityLayer == 0U);
static_assert(Tina::Scene::NoEntityGroup == 0U);

// A default-constructed entity is unnamed with default tag/layer/group.
static_assert(Tina::Scene::EntityMetadata{}.name.empty());
static_assert(Tina::Scene::EntityMetadata{}.tag == Tina::Scene::NoEntityTag);
static_assert(Tina::Scene::EntityMetadata{}.layer == Tina::Scene::DefaultEntityLayer);
static_assert(Tina::Scene::EntityMetadata{}.group == Tina::Scene::NoEntityGroup);

// A default filter selects everything.
static_assert(Tina::Scene::EntityMetadataFilter{}.filtersNothing());
static_assert(Tina::Scene::matchesEntityMetadataFilter(
    Tina::Scene::EntityMetadata{}, Tina::Scene::EntityMetadataFilter{}));

// Filtering on 0 is distinct from not filtering: it matches the default value and
// rejects anything else.
static_assert(!Tina::Scene::EntityMetadataFilter{.tag = Tina::Scene::NoEntityTag}
                   .filtersNothing());
static_assert(Tina::Scene::matchesEntityMetadataFilter(
    Tina::Scene::EntityMetadata{},
    Tina::Scene::EntityMetadataFilter{.tag = Tina::Scene::NoEntityTag}));
static_assert(!Tina::Scene::matchesEntityMetadataFilter(
    Tina::Scene::EntityMetadata{.tag = 5U},
    Tina::Scene::EntityMetadataFilter{.tag = Tina::Scene::NoEntityTag}));

// Every set field must match; a single mismatch rejects.
static_assert(Tina::Scene::matchesEntityMetadataFilter(
    Tina::Scene::EntityMetadata{.tag = 1U, .layer = 2U, .group = 3U},
    Tina::Scene::EntityMetadataFilter{.tag = 1U, .layer = 2U, .group = 3U}));
static_assert(!Tina::Scene::matchesEntityMetadataFilter(
    Tina::Scene::EntityMetadata{.tag = 1U, .layer = 2U, .group = 3U},
    Tina::Scene::EntityMetadataFilter{.tag = 1U, .layer = 9U}));
