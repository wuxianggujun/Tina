#include <tina/ui/UIVirtualGridView.hpp>

constexpr Tina::UI::UIVirtualGridViewCreateConfig DefaultVirtualGridConfig{};
static_assert(DefaultVirtualGridConfig.materializedItemCapacity == 64);
