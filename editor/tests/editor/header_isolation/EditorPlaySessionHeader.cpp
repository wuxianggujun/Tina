#include <tina/editor/EditorPlaySession.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Editor::EditorPlaySession>);
static_assert(std::is_move_constructible_v<Tina::Editor::EditorPlaySession>);
static_assert(Tina::Editor::EditorPlaySessionSnapshot{}.revision == 1U);
