#include <tina/editor/EditorProjectCreation.hpp>

#include <type_traits>

static_assert(std::is_move_constructible_v<Tina::Editor::EditorProjectWorkspace>);
