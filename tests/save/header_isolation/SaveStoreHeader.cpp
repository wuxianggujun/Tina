#include <tina/save/SaveStore.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Save::SaveStore>);
static_assert(!std::is_move_constructible_v<Tina::Save::SaveStore>);
