#include <tina/ui/UIDataGrid.hpp>

constexpr Tina::UI::UIDataGridCreateConfig DefaultDataGridConfig{};
static_assert(DefaultDataGridConfig.columnCapacity == 16);
static_assert(DefaultDataGridConfig.materializedRowCapacity == 64);
