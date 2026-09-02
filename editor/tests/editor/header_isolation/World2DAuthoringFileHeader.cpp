#include <tina/editor/World2DAuthoringFile.hpp>

#include <type_traits>

static_assert(std::is_function_v<decltype(Tina::Editor::saveWorld2DAuthoringDocument)>);
