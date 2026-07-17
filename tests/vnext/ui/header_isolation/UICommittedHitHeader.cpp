#include <tina/ui/UICommittedHit.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedHitEntry>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedHitView>);

constexpr Tina::UI::UICommittedHitView EmptyHit;
static_assert(EmptyHit.empty());
static_assert(EmptyHit.size() == 0U);
static_assert(EmptyHit.structureRevision() == 0U);
static_assert(EmptyHit.layoutRevision() == 0U);
static_assert(EmptyHit.paintOrderRevision() == 0U);
static_assert(EmptyHit.hitRevision() == 0U);
