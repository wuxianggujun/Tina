#include <tina/ui/UICommittedLayout.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedLayoutEntry>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedLayoutView>);

constexpr Tina::UI::UICommittedLayoutView EmptyLayout;
static_assert(EmptyLayout.empty());
static_assert(EmptyLayout.size() == 0U);
static_assert(EmptyLayout.structureRevision() == 0U);
static_assert(EmptyLayout.layoutRevision() == 0U);
