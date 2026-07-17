#include <tina/ui/UICommittedStructure.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedNodeEntry>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UICommittedStructureView>);

constexpr Tina::UI::UICommittedStructureView EmptyCommittedStructure{};
static_assert(EmptyCommittedStructure.empty());
static_assert(EmptyCommittedStructure.size() == 0);
static_assert(EmptyCommittedStructure.revision() == 0);
