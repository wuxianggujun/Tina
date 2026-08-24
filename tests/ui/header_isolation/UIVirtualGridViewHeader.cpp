#include <tina/ui/UIVirtualGridView.hpp>

constexpr Tina::UI::UIVirtualGridViewCreateConfig DefaultVirtualGridConfig{};
static_assert(DefaultVirtualGridConfig.materializedItemCapacity == 64);
static_assert(static_cast<Tina::u8>(Tina::UI::UIVirtualGridViewItemStatus::Ready) == 2);

constexpr Tina::UI::UIVirtualGridViewItemPresentation DefaultPresentation{};
static_assert(DefaultPresentation.secondaryLabel.empty());
static_assert(!DefaultPresentation.preview.has_value());
